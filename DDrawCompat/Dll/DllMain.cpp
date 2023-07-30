#include <string>

#include <Windows.h>
#include <DbgHelp.h>
#include <ShellScalingApi.h>
#include <timeapi.h>
#include <Uxtheme.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/Path.h>
#include <Common/ScopedCriticalSection.h>
#include <Common/Time.h>
#include <Config/Parser.h>
#include <Config/Settings/CrashDump.h>
#include <Config/Settings/DesktopResolution.h>
#include <Config/Settings/DpiAwareness.h>
#include <Config/Settings/FullscreenMode.h>
#include <D3dDdi/Hooks.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/Hooks.h>
#include <DDraw/LogUsedResourceFormat.h>
#include <Direct3d/Hooks.h>
#include <Dll/Dll.h>
#include <Gdi/Gdi.h>
#include <Gdi/GuiThread.h>
#include <Gdi/PresentationWindow.h>
#include <Gdi/VirtualScreen.h>
#include <Input/Input.h>
#include <Win32/DisplayMode.h>
#include <Win32/MemoryManagement.h>
#include <Win32/Registry.h>
#include <Win32/Thread.h>
#include <Win32/Version.h>
#include <Win32/Winmm.h>

HRESULT WINAPI SetAppCompatData(DWORD, DWORD);

namespace
{
	const DWORD DISABLE_MAX_WINDOWED_MODE = 12;

	Compat::CriticalSection g_crashDumpCs;
	std::filesystem::path g_crashDumpPath;

	template <typename Result, typename... Params>
	using FuncPtr = Result(WINAPI*)(Params...);

	template <FARPROC(Dll::Procs::* origFunc)>
	const char* getFuncName();

#define DEFINE_FUNC_NAME(func) template <> const char* getFuncName<&Dll::Procs::func>() { return #func; }
	VISIT_PUBLIC_DDRAW_PROCS(DEFINE_FUNC_NAME)
#undef  DEFINE_FUNC_NAME

	void installHooks();
	void onDirectDrawCreate(GUID* lpGUID, LPDIRECTDRAW* lplpDD, IUnknown* pUnkOuter);
	void onDirectDrawCreate(GUID* lpGUID, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter);

	template <FARPROC(Dll::Procs::* origFunc), typename OrigFuncPtrType, typename FirstParam, typename... Params>
	HRESULT WINAPI directDrawFunc(FirstParam firstParam, Params... params)
	{
		LOG_FUNC(getFuncName<origFunc>(), firstParam, params...);
		installHooks();
		if constexpr (&Dll::Procs::DirectDrawCreate == origFunc || &Dll::Procs::DirectDrawCreateEx == origFunc)
		{
			DDraw::DirectDraw::suppressEmulatedDirectDraw(firstParam);
		}
		HRESULT result = reinterpret_cast<OrigFuncPtrType>(Dll::g_origProcs.*origFunc)(firstParam, params...);
		if constexpr (&Dll::Procs::DirectDrawCreate == origFunc || &Dll::Procs::DirectDrawCreateEx == origFunc)
		{
			if (SUCCEEDED(result))
			{
				onDirectDrawCreate(firstParam, params...);
			}
		}
		return LOG_RESULT(result);
	}

	void installHooks()
	{
		if (!Dll::g_isHooked)
		{
			DDraw::SuppressResourceFormatLogs suppressResourceFormatLogs;
			LOG_INFO << "Installing display mode hooks";
			Win32::DisplayMode::installHooks();
			LOG_INFO << "Installing registry hooks";
			Win32::Registry::installHooks();
			LOG_INFO << "Installing Direct3D driver hooks";
			D3dDdi::installHooks();
			Gdi::VirtualScreen::init();

			CompatPtr<IDirectDraw> dd;
			HRESULT result = CALL_ORIG_PROC(DirectDrawCreate)(nullptr, &dd.getRef(), nullptr);
			if (FAILED(result))
			{
				LOG_INFO << "ERROR: Failed to create a DirectDraw object for hooking: " << Compat::hex(result);
				return;
			}

			CompatPtr<IDirectDraw7> dd7;
			result = CALL_ORIG_PROC(DirectDrawCreateEx)(
				nullptr, reinterpret_cast<void**>(&dd7.getRef()), IID_IDirectDraw7, nullptr);
			if (FAILED(result))
			{
				LOG_INFO << "ERROR: Failed to create a DirectDraw object for hooking: " << Compat::hex(result);
				return;
			}

			CompatVtable<IDirectDrawVtbl>::s_origVtable = *dd.get()->lpVtbl;
			result = dd->SetCooperativeLevel(dd, nullptr, DDSCL_NORMAL);
			if (SUCCEEDED(result))
			{
				CompatVtable<IDirectDraw7Vtbl>::s_origVtable = *dd7.get()->lpVtbl;
				dd7->SetCooperativeLevel(dd7, nullptr, DDSCL_NORMAL);
			}
			if (FAILED(result))
			{
				LOG_INFO << "ERROR: Failed to set the cooperative level for hooking: " << Compat::hex(result);
				return;
			}

			LOG_INFO << "Installing DirectDraw hooks";
			DDraw::installHooks(dd7);
			LOG_INFO << "Installing Direct3D hooks";
			Direct3d::installHooks(dd, dd7);
			LOG_INFO << "Installing GDI hooks";
			Gdi::installHooks();
			Compat::closeDbgEng();
			Gdi::GuiThread::start();
			LOG_INFO << "Finished installing hooks";
			Dll::g_isHooked = true;
		}
	}

	unsigned WINAPI installHooksThreadProc(LPVOID /*lpParameter*/)
	{
		installHooks();
		return 0;
	}

	bool isOtherDDrawWrapperLoaded()
	{
		const auto currentDllPath(Compat::getModulePath(Dll::g_currentModule));
		const auto ddrawDllPath(Compat::replaceFilename(currentDllPath, "ddraw.dll"));
		const auto dciman32DllPath(Compat::replaceFilename(currentDllPath, "dciman32.dll"));

		return (!Compat::isEqual(currentDllPath, ddrawDllPath) && GetModuleHandleW(ddrawDllPath.c_str())) ||
			(!Compat::isEqual(currentDllPath, dciman32DllPath) && GetModuleHandleW(dciman32DllPath.c_str()));
	}

	void logDpiAwareness(bool isSuccessful, DPI_AWARENESS_CONTEXT dpiAwareness, const char* funcName)
	{
		LOG_INFO << (isSuccessful ? "DPI awareness was successfully changed" : "Failed to change process DPI awareness")
			<< " to \"" << Config::dpiAwareness.convertToString(dpiAwareness) << "\" via " << funcName;
	}

	void onDirectDrawCreate(GUID* lpGUID, LPDIRECTDRAW* lplpDD, IUnknown* /*pUnkOuter*/)
	{
		return DDraw::DirectDraw::onCreate(lpGUID, *CompatPtr<IDirectDraw7>::from(*lplpDD));
	}

	void onDirectDrawCreate(GUID* lpGUID, LPVOID* lplpDD, REFIID /*iid*/, IUnknown* /*pUnkOuter*/)
	{
		return DDraw::DirectDraw::onCreate(lpGUID, *CompatPtr<IDirectDraw7>::from(static_cast<IDirectDraw7*>(*lplpDD)));
	}

	void printEnvironmentVariable(const char* var)
	{
		LOG_INFO << "Environment variable " << var << " = \"" << Dll::getEnvVar(var) << '"';
	}

	HRESULT WINAPI setAppCompatData(DWORD param1, DWORD param2)
	{
		LOG_FUNC("SetAppCompatData", param1, param2);
		if (DISABLE_MAX_WINDOWED_MODE == param1)
		{
			return LOG_RESULT(S_OK);
		}
		return LOG_RESULT(CALL_ORIG_PROC(SetAppCompatData)(param1, param2));
	}

	void setDpiAwareness()
	{
		auto dpiAwareness = Config::dpiAwareness.get();
		if (!dpiAwareness)
		{
			return;
		}

		HMODULE user32 = LoadLibrary("user32");
		auto isValidDpiAwarenessContext = reinterpret_cast<decltype(&IsValidDpiAwarenessContext)>(
			Compat::getProcAddress(user32, "IsValidDpiAwarenessContext"));
		auto setProcessDpiAwarenessContext = reinterpret_cast<decltype(&SetProcessDpiAwarenessContext)>(
			Compat::getProcAddress(user32, "SetProcessDpiAwarenessContext"));
		if (isValidDpiAwarenessContext && setProcessDpiAwarenessContext)
		{
			if (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == dpiAwareness &&
				!isValidDpiAwarenessContext(dpiAwareness))
			{
				dpiAwareness = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE;
			}
			
			if (DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED == dpiAwareness &&
				!isValidDpiAwarenessContext(dpiAwareness))
			{
				dpiAwareness = DPI_AWARENESS_CONTEXT_UNAWARE;
			}

			logDpiAwareness(setProcessDpiAwarenessContext(dpiAwareness), dpiAwareness, "SetProcessDpiAwarenessContext");
			return;
		}

		auto setProcessDpiAwareness = reinterpret_cast<decltype(&SetProcessDpiAwareness)>(
			Compat::getProcAddress(LoadLibrary("shcore"), "SetProcessDpiAwareness"));
		if (setProcessDpiAwareness)
		{
			HRESULT result = S_OK;
			if (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE == dpiAwareness ||
				DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == dpiAwareness)
			{
				dpiAwareness = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE;
				result = setProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
			}
			else if (DPI_AWARENESS_CONTEXT_SYSTEM_AWARE == dpiAwareness)
			{
				result = setProcessDpiAwareness(PROCESS_SYSTEM_DPI_AWARE);
			}
			else
			{
				dpiAwareness = DPI_AWARENESS_CONTEXT_UNAWARE;
				result = setProcessDpiAwareness(PROCESS_DPI_UNAWARE);
			}

			logDpiAwareness(SUCCEEDED(result), dpiAwareness, "SetProcessDpiAwareness");
			return;
		}

		if (DPI_AWARENESS_CONTEXT_UNAWARE == dpiAwareness ||
			DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED == dpiAwareness)
		{
			LOG_INFO << "DPI awareness was not changed";
		}

		logDpiAwareness(SetProcessDPIAware(), DPI_AWARENESS_CONTEXT_SYSTEM_AWARE, "SetProcessDPIAware");
	}

	LPTOP_LEVEL_EXCEPTION_FILTER WINAPI setUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter)
	{
		LOG_FUNC("SetUnhandledExceptionFilter", Compat::funcPtrToStr(lpTopLevelExceptionFilter));
		LOG_ONCE("Suppressed new unhandled exception filter: " << Compat::funcPtrToStr(lpTopLevelExceptionFilter));
		return LOG_RESULT(nullptr);
	}

	LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* ExceptionInfo)
	{
		Compat::ScopedCriticalSection lock(g_crashDumpCs);
		BOOL result = FALSE;
		DWORD error = 0;
		HANDLE dumpFile = INVALID_HANDLE_VALUE;

		LOG_INFO << "Terminating application due to unhandled exception: "
			<< Compat::hex(ExceptionInfo->ExceptionRecord->ExceptionCode);

		const auto writeDump = reinterpret_cast<decltype(&MiniDumpWriteDump)>(
			GetProcAddress(GetModuleHandle("dbghelp"), "MiniDumpWriteDump"));

		if (writeDump)
		{
			dumpFile = CreateFileW(g_crashDumpPath.native().c_str(),
				GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (INVALID_HANDLE_VALUE == dumpFile)
			{
				error = GetLastError();
			}
			else
			{

				MINIDUMP_EXCEPTION_INFORMATION mei = {};
				mei.ThreadId = GetCurrentThreadId();
				mei.ExceptionPointers = ExceptionInfo;
				result = writeDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile,
					static_cast<MINIDUMP_TYPE>(Config::crashDump.get()), &mei, nullptr, nullptr);
				if (!result)
				{
					error = GetLastError();
				}
				CloseHandle(dumpFile);
			}
		}

		if (result)
		{
			LOG_INFO << "Crash dump has been written to: " << g_crashDumpPath.native().c_str();
		}
		else if (!writeDump)
		{
			LOG_INFO << "Failed to load procedure MiniDumpWriteDump to create a crash dump";
		}
		else if (INVALID_HANDLE_VALUE == dumpFile)
		{
			LOG_INFO << "Failed to create crash dump file: " << Compat::hex(error);
		}
		else
		{
			LOG_INFO << "Failed to write crash dump: " << Compat::hex(error);
		}

		TerminateProcess(GetCurrentProcess(), 0);
		return EXCEPTION_CONTINUE_SEARCH;
	}
}

#define	LOAD_ORIG_PROC(proc) \
	Dll::g_origProcs.proc = Compat::getProcAddress(origModule, #proc);

#define HOOK_DDRAW_PROC(proc) \
	Compat::hookFunction( \
		reinterpret_cast<void*&>(Dll::g_origProcs.proc), \
		static_cast<decltype(&proc)>(&directDrawFunc<&Dll::Procs::proc, decltype(&proc)>), \
		#proc);

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	static bool skipDllMain = false;
	if (skipDllMain)
	{
		return TRUE;
	}

	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		Dll::g_currentModule = hinstDLL;
		if (isOtherDDrawWrapperLoaded())
		{
			skipDllMain = true;
			return TRUE;
		}

		auto processPath(Compat::getModulePath(nullptr));
		LOG_INFO << "Process path: " << processPath.u8string();

		auto currentDllPath(Compat::getModulePath(hinstDLL));
		LOG_INFO << "Loading DDrawCompat " << (lpvReserved ? "statically" : "dynamically") << " from " << currentDllPath.u8string();
		printEnvironmentVariable("__COMPAT_LAYER");

		Config::Parser::loadAllConfigFiles(processPath);
		Compat::Log::initLogging(processPath, Config::logLevel.get());

		if (Config::Settings::CrashDump::OFF != Config::crashDump.get())
		{
			g_crashDumpPath = processPath;
			if (Compat::isEqual(g_crashDumpPath.extension(), ".exe"))
			{
				g_crashDumpPath.replace_extension();
			}
			g_crashDumpPath.replace_filename(L"DDrawCompat-" + g_crashDumpPath.filename().native());
			g_crashDumpPath += ".dmp";

			HOOK_FUNCTION(kernel32, SetUnhandledExceptionFilter, setUnhandledExceptionFilter);
			LOG_INFO << "Installing unhandled exception filter for automatic crash dumps";
			auto prevFilter = CALL_ORIG_FUNC(SetUnhandledExceptionFilter)(&unhandledExceptionFilter);
			if (prevFilter)
			{
				LOG_INFO << "Replaced previous unhandled exception filter: " << Compat::funcPtrToStr(prevFilter);
			}
		}

		auto systemPath(Compat::getSystemPath());
		if (Compat::isEqual(currentDllPath.parent_path(), systemPath))
		{
			LOG_INFO << "DDrawCompat cannot be installed in the Windows system directory";
			return FALSE;
		}

		Dll::g_origDDrawModule = LoadLibraryW((systemPath / "ddraw.dll").c_str());
		if (!Dll::g_origDDrawModule)
		{
			LOG_INFO << "ERROR: Failed to load system ddraw.dll from " << systemPath.u8string();
			return FALSE;
		}

		Dll::pinModule(Dll::g_origDDrawModule);
		Dll::pinModule(Dll::g_currentModule);

		HMODULE origModule = Dll::g_origDDrawModule;
		VISIT_DDRAW_PROCS(LOAD_ORIG_PROC);

		Dll::g_origDciman32Module = LoadLibraryW((systemPath / "dciman32.dll").c_str());
		if (Dll::g_origDciman32Module)
		{
			origModule = Dll::g_origDciman32Module;
			VISIT_DCIMAN32_PROCS(LOAD_ORIG_PROC);
		}

		Dll::g_jmpTargetProcs = Dll::g_origProcs;

		VISIT_PUBLIC_DDRAW_PROCS(HOOK_DDRAW_PROC);
		Compat::hookFunction(reinterpret_cast<void*&>(Dll::g_origProcs.SetAppCompatData),
			static_cast<decltype(&SetAppCompatData)>(&setAppCompatData), "SetAppCompatData");

		Input::installHooks();
		Win32::MemoryManagement::installHooks();
		Win32::Thread::installHooks();
		Win32::Version::installHooks();
		Win32::Winmm::installHooks();
		Compat::closeDbgEng();

		CALL_ORIG_FUNC(timeBeginPeriod)(1);
		setDpiAwareness();
		SetThemeAppProperties(0);
		Time::init();
		Win32::Thread::applyConfig();

		if (Config::Settings::FullscreenMode::EXCLUSIVE == Config::fullscreenMode.get())
		{
			CALL_ORIG_PROC(SetAppCompatData)(DISABLE_MAX_WINDOWED_MODE, 1);
		}

		if (Config::Settings::DesktopResolution::DESKTOP != Config::desktopResolution.get())
		{
			Dll::createThread(&installHooksThreadProc, nullptr, THREAD_PRIORITY_TIME_CRITICAL);
		}

		LOG_INFO << "DDrawCompat loaded successfully";
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{
		LOG_INFO << "DDrawCompat detached successfully";
	}
	else if (fdwReason == DLL_THREAD_DETACH)
	{
		Gdi::dllThreadDetach();
	}

	return TRUE;
}
