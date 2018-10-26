#pragma once

#include "Gdi.h"

namespace Gdi
{
	namespace WinProc
	{
		void dllThreadDetach();
		void installHooks();
		void watchWindowPosChanges(WindowPosChangeNotifyFunc notifyFunc);
		void uninstallHooks();
	}
}
