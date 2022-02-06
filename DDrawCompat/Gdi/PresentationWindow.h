#pragma once

#include <Windows.h>

namespace Gdi
{
	namespace PresentationWindow
	{
		HWND create(HWND owner);

		void installHooks();
	}
}
