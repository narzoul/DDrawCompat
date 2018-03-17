#pragma once

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

namespace Gdi
{
	namespace DcFunctions
	{
		HRGN getVisibleWindowRgn(HWND hwnd);
		void installHooks();
	}
}
