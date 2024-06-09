#pragma once

#include <Windows.h>

namespace Gdi
{
	namespace Cursor
	{
		void clip(POINT& pt);
		CURSORINFO getEmulatedCursorInfo();
		void installHooks();
		bool isEmulated();
		HCURSOR setCursor(HCURSOR cursor);
		void setMonitorClipRect(const RECT& rect);
		void setEmulated(bool isEmulated);
		void update();
	}
}
