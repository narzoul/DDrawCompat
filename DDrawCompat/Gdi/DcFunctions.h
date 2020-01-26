#pragma once

#include <Windows.h>

namespace Gdi
{
	namespace DcFunctions
	{
		HRGN getVisibleWindowRgn(HWND hwnd);
		void installHooks();
	}
}
