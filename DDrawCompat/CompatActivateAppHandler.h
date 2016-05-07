#pragma once

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

struct IUnknown;

namespace CompatActivateAppHandler
{
	void installHooks();
	bool isActive();
	void setFullScreenCooperativeLevel(IUnknown* dd, HWND hwnd, DWORD flags);
	void uninstallHooks();
}
