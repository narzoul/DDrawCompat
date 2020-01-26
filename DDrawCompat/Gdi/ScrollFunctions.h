#pragma once

#include <Windows.h>

namespace Gdi
{
	namespace ScrollFunctions
	{
		void installHooks();
		void updateScrolledWindow(HWND hwnd);
	}
}
