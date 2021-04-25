#include <string>

#include <Windows.h>
#include <ShellScalingApi.h>
#include <timeapi.h>
#include <Uxtheme.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/Path.h>
#include <Common/Time.h>
#include <D3dDdi/Hooks.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/Hooks.h>
#include <Direct3d/Hooks.h>
#include <Dll/Dll.h>
#include <Gdi/Gdi.h>
#include <Gdi/PresentationWindow.h>
#include <Gdi/VirtualScreen.h>
#include <Win32/DisplayMode.h>
#include <Win32/MemoryManagement.h>
#include <Win32/MsgHooks.h>
#include <Win32/Registry.h>
#include <Win32/WaitFunctions.h>

HRESULT WINAPI SetAppCompatData(DWORD, DWORD);

namespace
{
	template <typename Result, typename... Params>
	using FuncPtr = Result(WINAPI*)(Params...);

	template <FARPROC(Dll::Procs::* origFunc)>
	const char* getFuncName();

#define DEFINE_FUNC_NAME(func) template <> const char* getFuncName<&Dll::Procs::func>() { return #func; }
	VISIT_PUBLIC_DDRAW_PROCS(DEFINE_FUNC_NAME)
#undef  DEFINE_FUNC_NAME

	void installHooks();

	template <FARPROC(Dll::Procs::* origFunc), typename OrigFuncPtrType, typename FirstParam, typename... Params>
	HRESULT WINAPI directDrawFunc(FirstParam firstParam, Params... params)
	{
		LOG_FUNC(getFuncName<origFunc>(), firstParam, params...);
		installHooks();
		suppressEmulatedDirectDraw(firstParam);
		return LOG_RESULT(reinterpret_cast<OrigFuncPtrType>(Dll::g_origProcs.*origFunc)(firstParam, params...));
	}

	void installHooks()
	{
		static bool isAlreadyInstalled = false;
		if (!isAlreadyInstalled)
		{
			Compat::Log() << "Installing display mode hooks";
			Win32::DisplayMode::installHooks();
			Compat::Log() << "Installing registry hooks";
			Win32::Registry::installHooks();
			Compat::Log() << "Installing Direct3D driver hooks";
			D3dDdi::installHooks();
			Compat::Log() << "Installing Win32 hooks";
			Win32::WaitFunctions::installHooks();
			Gdi::VirtualScreen::init();

			CompatPtr<IDirectDraw> dd;
			CALL_ORIG_PROC(DirectDrawCreate)(nullptr, &dd.getRef(), nullptr);
			CompatPtr<IDirectDraw7> dd7;
			CALL_ORIG_PROC(DirectDrawCreateEx)(nullptr, reinterpret_cast<void**>(&dd7.getRef()), IID_IDirectDraw7, nullptr);
			if (!dd || !dd7)
			{
				Compat::Log() << "ERROR: Failed to create a DirectDraw object for hooking";
				return;
			}

			CompatVtable<IDirectDrawVtbl>::s_origVtable = *dd.get()->lpVtbl;
			HRESULT result = dd->SetCooperativeLevel(dd, nullptr, DDSCL_NORMAL);
			if (SUCCEEDED(result))
			{
				CompatVtable<IDirectDraw7Vtbl>::s_origVtable = *dd7.get()->lpVtbl;
				dd7->SetCooperativeLevel(dd7, nullptr, DDSCL_NORMAL);
			}
			if (FAILED(result))
			{
				Compat::Log() << "ERROR: Failed to set the cooperative level for hooking: " << Compat::hex(result);
				return;
			}

			Compat::Log() << "Installing DirectDraw hooks";
			DDraw::installHooks(dd7);
			Compat::Log() << "Installing Direct3D hooks";
			Direct3d::installHooks(dd, dd7);
			Compat::Log() << "Installing GDI hooks";
			Gdi::installHooks();
			Compat::closeDbgEng();
			Gdi::PresentationWindow::startThread();
			Compat::Log() << "Finished installing hooks";
			isAlreadyInstalled = true;
		}
	}

	bool isOtherDDrawWrapperLoaded()
	{
		const auto currentDllPath(Compat::getModulePath(Dll::g_currentModule));
		const auto ddrawDllPath(Compat::replaceFilename(currentDllPath, "ddraw.dll"));
		const auto dciman32DllPath(Compat::replaceFilename(currentDllPath, "dciman32.dll"));

		return (!Compat::isEqual(currentDllPath, ddrawDllPath) && GetModuleHandleW(ddrawDllPath.c_str())) ||
			(!Compat::isEqual(currentDllPath, dciman32DllPath) && GetModuleHandleW(dciman32DllPath.c_str()));
	}

	void printEnvironmentVariable(const char* var)
	{
		const DWORD size = GetEnvironmentVariable(var, nullptr, 0);
		std::string value(size, 0);
		if (!value.empty())
		{
			GetEnvironmentVariable(var, &value.front(), size);
			value.pop_back();
		}
		Compat::Log() << "Environment variable " << var << " = \"" << value << '"';
	}

	void setDpiAwareness()
	{
		HMODULE shcore = LoadLibrary("shcore");
		if (shcore)
		{
			auto setProcessDpiAwareness = reinterpret_cast<decltype(&SetProcessDpiAwareness)>(
				Compat::getProcAddress(shcore, "SetProcessDpiAwareness"));
			if (setProcessDpiAwareness && SUCCEEDED(setProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE)))
			{
				return;
			}
		}
		SetProcessDPIAware();
	}

	template <typename Param>
	void suppressEmulatedDirectDraw(Param)
	{
	}

	void suppressEmulatedDirectDraw(GUID*& guid)
	{
		DDraw::DirectDraw::suppressEmulatedDirectDraw(guid);
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
		Compat::Log::initLogging(processPath);

		Compat::Log() << "Process path: " << processPath.u8string();
		printEnvironmentVariable("__COMPAT_LAYER");
		auto currentDllPath(Compat::getModulePath(hinstDLL));
		Compat::Log() << "Loading DDrawCompat " << (lpvReserved ? "statically" : "dynamically") << " from " << currentDllPath.u8string();

		auto systemPath(Compat::getSystemPath());
		if (Compat::isEqual(currentDllPath.parent_path(), systemPath))
		{
			Compat::Log() << "DDrawCompat cannot be installed in the Windows system directory";
			return FALSE;
		}

		Dll::g_origDDrawModule = LoadLibraryW((systemPath / "ddraw.dll").c_str());
		if (!Dll::g_origDDrawModule)
		{
			Compat::Log() << "ERROR: Failed to load system ddraw.dll from " << systemPath.u8string();
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

		VISIT_PUBLIC_DDRAW_PROCS(HOOK_DDRAW_PROC)

		const BOOL disablePriorityBoost = TRUE;
		SetProcessPriorityBoost(GetCurrentProcess(), disablePriorityBoost);
		SetProcessAffinityMask(GetCurrentProcess(), 1);
		timeBeginPeriod(1);
		setDpiAwareness();
		SetThemeAppProperties(0);

		Win32::MemoryManagement::installHooks();
		Win32::MsgHooks::installHooks();
		Time::init();
		Compat::closeDbgEng();

		const DWORD disableMaxWindowedMode = 12;
		CALL_ORIG_PROC(SetAppCompatData)(disableMaxWindowedMode, 0);

		Compat::Log() << "DDrawCompat loaded successfully";
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{
		Compat::Log() << "DDrawCompat detached successfully";
	}
	else if (fdwReason == DLL_THREAD_DETACH)
	{
		Gdi::dllThreadDetach();
	}

	return TRUE;
}
