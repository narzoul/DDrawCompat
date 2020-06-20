#include <string>
#include <vector>

#include <Windows.h>
#include <Psapi.h>
#include <ShellScalingApi.h>
#include <timeapi.h>
#include <Uxtheme.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/Time.h>
#include <D3dDdi/Hooks.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/Hooks.h>
#include <Direct3d/Hooks.h>
#include <Dll/Dll.h>
#include <Gdi/Gdi.h>
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

	HMODULE g_origDDrawModule = nullptr;
	HMODULE g_origDciman32Module = nullptr;

	template <FARPROC(Dll::Procs::* origFunc)>
	const char* getFuncName();

#define DEFINE_FUNC_NAME(func) template <> const char* getFuncName<&Dll::Procs::func>() { return #func; }
	VISIT_PUBLIC_DDRAW_PROCS(DEFINE_FUNC_NAME)
#undef  DEFINE_FUNC_NAME

	void installHooks();

	template <FARPROC(Dll::Procs::* origFunc), typename Result, typename FirstParam, typename... Params>
	Result WINAPI directDrawFunc(FirstParam firstParam, Params... params)
	{
		LOG_FUNC(getFuncName<origFunc>(), firstParam, params...);
		installHooks();
		suppressEmulatedDirectDraw(firstParam);
		return LOG_RESULT(reinterpret_cast<FuncPtr<Result, FirstParam, Params...>>(Dll::g_origProcs.*origFunc)(
			firstParam, params...));
	}

	template <FARPROC(Dll::Procs::* origFunc), typename Result, typename... Params>
	FuncPtr<Result, Params...> getDirectDrawFuncPtr(FuncPtr<Result, Params...>)
	{
		return &directDrawFunc<origFunc, Result, Params...>;
	}

	std::string getDirName(const std::string& path)
	{
		return path.substr(0, path.find_last_of('\\'));
	}

	std::string getFileName(const std::string& path)
	{
		auto lastSeparatorPos = path.find_last_of('\\');
		return std::string::npos == lastSeparatorPos ? path : path.substr(lastSeparatorPos + 1, std::string::npos);
	}

	std::string getModulePath(HMODULE module)
	{
		char path[MAX_PATH] = {};
		GetModuleFileName(module, path, sizeof(path));
		return path;
	}

	std::vector<HMODULE> getProcessModules(HANDLE process)
	{
		std::vector<HMODULE> modules(10000);
		DWORD bytesNeeded = 0;
		if (EnumProcessModules(process, modules.data(), modules.size(), &bytesNeeded))
		{
			modules.resize(bytesNeeded / sizeof(modules[0]));
		}
		return modules;
	}

	std::string getSystemDirectory()
	{
		char path[MAX_PATH] = {};
		GetSystemDirectory(path, sizeof(path));
		return path;
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
			D3dDdi::installHooks(g_origDDrawModule);
			Compat::Log() << "Installing Win32 hooks";
			Win32::WaitFunctions::installHooks();
			Gdi::VirtualScreen::init();

			CompatPtr<IDirectDraw> dd;
			CALL_ORIG_PROC(DirectDrawCreate)(nullptr, &dd.getRef(), nullptr);
			CompatPtr<IDirectDraw> dd7;
			CALL_ORIG_PROC(DirectDrawCreateEx)(nullptr, reinterpret_cast<void**>(&dd7.getRef()), IID_IDirectDraw7, nullptr);
			if (!dd || !dd7)
			{
				Compat::Log() << "ERROR: Failed to create a DirectDraw object for hooking";
				return;
			}

			HRESULT result = dd->SetCooperativeLevel(dd, nullptr, DDSCL_NORMAL);
			if (SUCCEEDED(result))
			{
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
			Compat::Log() << "Finished installing hooks";
			isAlreadyInstalled = true;
		}
	}

	bool isEqualPath(const std::string& p1, const std::string& p2)
	{
		return 0 == _strcmpi(p1.c_str(), p2.c_str());
	}

	bool isOtherDDrawWrapperLoaded()
	{
		auto currentDllPath = getModulePath(Dll::g_currentModule);
		auto systemDirectory = getSystemDirectory();
		auto processModules = getProcessModules(GetCurrentProcess());
		for (HMODULE module : processModules)
		{
			auto path = getModulePath(module);
			auto fileName = getFileName(path);
			if ((isEqualPath(fileName, "ddraw.dll") || isEqualPath(fileName, "dciman32.dll")) &&
				!isEqualPath(path, currentDllPath) &&
				!isEqualPath(getDirName(path), systemDirectory))
			{
				return true;
			}
		}
		return false;
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
		DDraw::suppressEmulatedDirectDraw(guid);
	}
}

#define	LOAD_ORIG_PROC(proc) \
	Dll::g_origProcs.proc = Compat::getProcAddress(origModule, #proc);

#define HOOK_DDRAW_PROC(proc) \
	Compat::hookFunction( \
		reinterpret_cast<void*&>(Dll::g_origProcs.proc), \
		getDirectDrawFuncPtr<&Dll::Procs::proc>(static_cast<decltype(&proc)>(nullptr)), \
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

		auto processPath = getModulePath(nullptr);
		Compat::Log::initLogging(getFileName(processPath));

		Compat::Log() << "Process path: " << processPath;
		printEnvironmentVariable("__COMPAT_LAYER");
		auto currentDllPath = getModulePath(hinstDLL);
		Compat::Log() << "Loading DDrawCompat " << (lpvReserved ? "statically" : "dynamically") << " from " << currentDllPath;

		auto systemDirectory = getSystemDirectory();
		if (isEqualPath(getDirName(currentDllPath), systemDirectory))
		{
			Compat::Log() << "DDrawCompat cannot be installed in the Windows system directory";
			return FALSE;
		}

		auto systemDDrawDllPath = systemDirectory + "\\ddraw.dll";
		g_origDDrawModule = LoadLibrary(systemDDrawDllPath.c_str());
		if (!g_origDDrawModule)
		{
			Compat::Log() << "ERROR: Failed to load system ddraw.dll from " << systemDDrawDllPath;
			return FALSE;
		}

		HMODULE origModule = g_origDDrawModule;
		VISIT_DDRAW_PROCS(LOAD_ORIG_PROC);

		auto systemDciman32DllPath = systemDirectory + "\\dciman32.dll";
		g_origDciman32Module = LoadLibrary(systemDciman32DllPath.c_str());
		if (g_origDciman32Module)
		{
			origModule = g_origDciman32Module;
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

		const DWORD disableMaxWindowedMode = 12;
		CALL_ORIG_PROC(SetAppCompatData)(disableMaxWindowedMode, 0);

		Compat::Log() << "DDrawCompat loaded successfully";
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{
		Compat::Log() << "Detaching DDrawCompat due to " << (lpvReserved ? "process termination" : "FreeLibrary");
		if (!lpvReserved)
		{
			DDraw::uninstallHooks();
			D3dDdi::uninstallHooks();
			Gdi::uninstallHooks();
			Compat::unhookAllFunctions();
			FreeLibrary(g_origDciman32Module);
			FreeLibrary(g_origDDrawModule);
		}
		timeEndPeriod(1);
		Compat::Log() << "DDrawCompat detached successfully";
	}
	else if (fdwReason == DLL_THREAD_DETACH)
	{
		Gdi::dllThreadDetach();
	}

	return TRUE;
}
