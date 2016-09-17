#pragma once

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

namespace DDraw
{
	namespace ActivateAppHandler
	{
		void installHooks();
		bool isActive();
		void setFullScreenCooperativeLevel(HWND hwnd, DWORD flags);
		void uninstallHooks();
	}
}
