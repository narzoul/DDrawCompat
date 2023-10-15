#pragma once

#include <Windows.h>

namespace Gdi
{
	namespace PresentationWindow
	{
		HWND create(HWND owner, bool dpiAware = false);

		void installHooks();
	}
}
