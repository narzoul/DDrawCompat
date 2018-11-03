#define WIN32_LEAN_AND_MEAN

#include <string>

#include <Windows.h>
#include <timeapi.h>
#include <Uxtheme.h>

#include "Common/Hook.h"
#include "Common/Log.h"
#include "Common/Time.h"
#include "D3dDdi/Hooks.h"
#include "DDraw/DirectDraw.h"
#include "DDraw/Hooks.h"
#include "Direct3d/Hooks.h"
#include "Dll/Procs.h"
#include "Gdi/Gdi.h"
#include "Gdi/VirtualScreen.h"
#include "Win32/DisplayMode.h"
#include "Win32/FontSmoothing.h"
#include "Win32/MsgHooks.h"
#include "Win32/Registry.h"

struct IDirectInput;

HRESULT WINAPI SetAppCompatData(DWORD, DWORD);

namespace
{
	HMODULE g_origDDrawModule = nullptr;
	HMODULE g_origDInputModule = nullptr;

	void installHooks()
	{
		static bool isAlreadyInstalled = false;
		if (!isAlreadyInstalled)
		{
			Win32::DisplayMode::disableDwm8And16BitMitigation();
			Compat::Log() << "Installing registry hooks";
			Win32::Registry::installHooks();
			Compat::Log() << "Installing Direct3D driver hooks";
			D3dDdi::installHooks();
			Compat::Log() << "Installing display mode hooks";
			Win32::DisplayMode::installHooks(g_origDDrawModule);
			Gdi::VirtualScreen::init();
			Compat::Log() << "Installing DirectDraw hooks";
			DDraw::installHooks();
			Compat::Log() << "Installing Direct3D hooks";
			Direct3d::installHooks();
			Compat::Log() << "Installing GDI hooks";
			Gdi::installHooks();
			Compat::Log() << "Finished installing hooks";
			isAlreadyInstalled = true;
		}
	}

	bool loadLibrary(const std::string& systemDirectory, const std::string& dllName, HMODULE& module)
	{
		const std::string systemDllPath = systemDirectory + '\\' + dllName;

		module = LoadLibrary(systemDllPath.c_str());
		if (!module)
		{
			Compat::Log() << "Failed to load system " << dllName << " from " << systemDllPath;
			return false;
		}

		return true;
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
}

#define	LOAD_ORIGINAL_PROC(procName) \
	Dll::g_origProcs.procName = Compat::getProcAddress(g_origDDrawModule, #procName);

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		char currentProcessPath[MAX_PATH] = {};
		GetModuleFileName(nullptr, currentProcessPath, MAX_PATH);
		Compat::Log() << "Process path: " << currentProcessPath;

		printEnvironmentVariable("__COMPAT_LAYER");

		char currentDllPath[MAX_PATH] = {};
		GetModuleFileName(hinstDLL, currentDllPath, MAX_PATH);
		Compat::Log() << "Loading DDrawCompat " << (lpvReserved ? "statically" : "dynamically")
			<< " from " << currentDllPath;

		char systemDirectory[MAX_PATH] = {};
		GetSystemDirectory(systemDirectory, MAX_PATH);

		std::string systemDDrawDllPath = std::string(systemDirectory) + "\\ddraw.dll";
		if (0 == _stricmp(currentDllPath, systemDDrawDllPath.c_str()))
		{
			Compat::Log() << "DDrawCompat cannot be installed as the system ddraw.dll";
			return FALSE;
		}

		if (!loadLibrary(systemDirectory, "ddraw.dll", g_origDDrawModule) ||
			!loadLibrary(systemDirectory, "dinput.dll", g_origDInputModule))
		{
			return FALSE;
		}

		VISIT_ALL_PROCS(LOAD_ORIGINAL_PROC);
		Dll::g_origProcs.DirectInputCreateA = GetProcAddress(g_origDInputModule, "DirectInputCreateA");

		const BOOL disablePriorityBoost = TRUE;
		SetProcessPriorityBoost(GetCurrentProcess(), disablePriorityBoost);
		SetProcessAffinityMask(GetCurrentProcess(), 1);
		timeBeginPeriod(1);
		SetThemeAppProperties(0);

		Compat::redirectIatHooks("ddraw.dll", "DirectDrawCreate",
			Compat::getProcAddress(hinstDLL, "DirectDrawCreate"));
		Compat::redirectIatHooks("ddraw.dll", "DirectDrawCreateEx",
			Compat::getProcAddress(hinstDLL, "DirectDrawCreateEx"));
		Win32::FontSmoothing::g_origSystemSettings = Win32::FontSmoothing::getSystemSettings();
		Win32::MsgHooks::installHooks();
		Time::init();

		const DWORD disableMaxWindowedMode = 12;
		CALL_ORIG_PROC(SetAppCompatData, disableMaxWindowedMode, 0);

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
			FreeLibrary(g_origDInputModule);
			FreeLibrary(g_origDDrawModule);
		}
		Win32::FontSmoothing::setSystemSettingsForced(Win32::FontSmoothing::g_origSystemSettings);
		timeEndPeriod(1);
		Compat::Log() << "DDrawCompat detached successfully";
	}
	else if (fdwReason == DLL_THREAD_DETACH)
	{
		Gdi::dllThreadDetach();
	}

	return TRUE;
}

extern "C" HRESULT WINAPI DirectDrawCreate(
	GUID* lpGUID,
	LPDIRECTDRAW* lplpDD,
	IUnknown* pUnkOuter)
{
	LOG_FUNC(__func__, lpGUID, lplpDD, pUnkOuter);
	installHooks();
	DDraw::suppressEmulatedDirectDraw(lpGUID);
	return LOG_RESULT(CALL_ORIG_PROC(DirectDrawCreate, lpGUID, lplpDD, pUnkOuter));
}

extern "C" HRESULT WINAPI DirectDrawCreateEx(
	GUID* lpGUID,
	LPVOID* lplpDD,
	REFIID iid,
	IUnknown* pUnkOuter)
{
	LOG_FUNC(__func__, lpGUID, lplpDD, iid, pUnkOuter);
	installHooks();
	DDraw::suppressEmulatedDirectDraw(lpGUID);
	return LOG_RESULT(CALL_ORIG_PROC(DirectDrawCreateEx, lpGUID, lplpDD, iid, pUnkOuter));
}

extern "C" HRESULT WINAPI DirectInputCreateA(
	HINSTANCE hinst,
	DWORD dwVersion,
	IDirectInput** lplpDirectInput,
	LPUNKNOWN punkOuter)
{
	LOG_FUNC(__func__, hinst, dwVersion, lplpDirectInput, punkOuter);
	return LOG_RESULT(CALL_ORIG_PROC(DirectInputCreateA, hinst, dwVersion, lplpDirectInput, punkOuter));
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
	LOG_FUNC(__func__, rclsid, riid, ppv);
	LOG_ONCE("COM instantiation of DirectDraw detected");
	installHooks();
	return LOG_RESULT(CALL_ORIG_PROC(DllGetClassObject, rclsid, riid, ppv));
}
