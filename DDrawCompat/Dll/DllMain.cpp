#include <string>

#include <Windows.h>
#include <ShellScalingApi.h>
#include <timeapi.h>
#include <Uxtheme.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/Path.h>
#include <Common/Time.h>
#include <Config/Config.h>
#include <Config/Parser.h>
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
#include <Win32/Thread.h>

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
	void onDirectDrawCreate(GUID* lpGUID, LPDIRECTDRAW* lplpDD, IUnknown* pUnkOuter);
	void onDirectDrawCreate(GUID* lpGUID, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter);

	template <FARPROC(Dll::Procs::* origFunc), typename OrigFuncPtrType, typename FirstParam, typename... Params>
	HRESULT WINAPI directDrawFunc(FirstParam firstParam, Params... params)
	{
		LOG_FUNC(getFuncName<origFunc>(), params...);
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
		static bool isAlreadyInstalled = false;
		if (!isAlreadyInstalled)
		{
			Compat::Log() << "Installing display mode hooks";
			Win32::DisplayMode::installHooks();
			Compat::Log() << "Installing registry hooks";
			Win32::Registry::installHooks();
			Compat::Log() << "Installing Direct3D driver hooks";
			D3dDdi::installHooks();
			Gdi::VirtualScreen::init();

			CompatPtr<IDirectDraw> dd;
			HRESULT result = CALL_ORIG_PROC(DirectDrawCreate)(nullptr, &dd.getRef(), nullptr);
			if (FAILED(result))
			{
				Compat::Log() << "ERROR: Failed to create a DirectDraw object for hooking: " << Compat::hex(result);
				return;
			}

			CompatPtr<IDirectDraw7> dd7;
			result = CALL_ORIG_PROC(DirectDrawCreateEx)(
				nullptr, reinterpret_cast<void**>(&dd7.getRef()), IID_IDirectDraw7, nullptr);
			if (FAILED(result))
			{
				Compat::Log() << "ERROR: Failed to create a DirectDraw object for hooking: " << Compat::hex(result);
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
		Compat::Log() << "Environment variable " << var << " = \"" << Dll::getEnvVar(var) << '"';
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

		auto currentDllPath(Compat::getModulePath(hinstDLL));
		Compat::Log() << "Loading DDrawCompat " << (lpvReserved ? "statically" : "dynamically") << " from " << currentDllPath.u8string();
		printEnvironmentVariable("__COMPAT_LAYER");

		Config::Parser::loadAllConfigFiles(processPath);

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

		VISIT_PUBLIC_DDRAW_PROCS(HOOK_DDRAW_PROC);

		Win32::MemoryManagement::installHooks();
		Win32::MsgHooks::installHooks();
		Win32::Thread::installHooks();
		Compat::closeDbgEng();

		timeBeginPeriod(1);
		setDpiAwareness();
		SetThemeAppProperties(0);
		Time::init();
		Win32::Thread::applyConfig();

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
