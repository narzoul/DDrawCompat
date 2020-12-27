#pragma once

#include <ostream>
#include <string>

#include <Windows.h>

std::ostream& operator<<(std::ostream& os, const COMPAREITEMSTRUCT& cis);
std::ostream& operator<<(std::ostream& os, const COPYDATASTRUCT& cds);
std::ostream& operator<<(std::ostream& os, const CREATESTRUCTA& cs);
std::ostream& operator<<(std::ostream& os, const CREATESTRUCTW& cs);
std::ostream& operator<<(std::ostream& os, const CWPSTRUCT& cwp);
std::ostream& operator<<(std::ostream& os, const CWPRETSTRUCT& cwrp);
std::ostream& operator<<(std::ostream& os, const DELETEITEMSTRUCT& dis);
std::ostream& operator<<(std::ostream& os, const DEVMODEA& dm);
std::ostream& operator<<(std::ostream& os, const DEVMODEW& dm);
std::ostream& operator<<(std::ostream& os, const DRAWITEMSTRUCT& dis);
std::ostream& operator<<(std::ostream& os, const GESTURENOTIFYSTRUCT& gns);
std::ostream& operator<<(std::ostream& os, HDC dc);
std::ostream& operator<<(std::ostream& os, const HELPINFO& hi);
std::ostream& operator<<(std::ostream& os, HFONT font);
std::ostream& operator<<(std::ostream& os, HRGN rgn);
std::ostream& operator<<(std::ostream& os, HWND hwnd);
std::ostream& operator<<(std::ostream& os, const LOGFONT& lf);
std::ostream& operator<<(std::ostream& os, const MDICREATESTRUCTA& mcs);
std::ostream& operator<<(std::ostream& os, const MDICREATESTRUCTW& mcs);
std::ostream& operator<<(std::ostream& os, const MDINEXTMENU& mnm);
std::ostream& operator<<(std::ostream& os, const MEASUREITEMSTRUCT& mis);
std::ostream& operator<<(std::ostream& os, const MEMORYSTATUS& ms);
std::ostream& operator<<(std::ostream& os, const MENUGETOBJECTINFO& mgoi);
std::ostream& operator<<(std::ostream& os, const MINMAXINFO& mmi);
std::ostream& operator<<(std::ostream& os, const MSG& msg);
std::ostream& operator<<(std::ostream& os, const NCCALCSIZE_PARAMS& nccs);
std::ostream& operator<<(std::ostream& os, const NMHDR& nm);
std::ostream& operator<<(std::ostream& os, const POINT& p);
std::ostream& operator<<(std::ostream& os, const RECT& rect);
std::ostream& operator<<(std::ostream& os, const SIZE& size);
std::ostream& operator<<(std::ostream& os, const STYLESTRUCT& ss);
std::ostream& operator<<(std::ostream& os, const TITLEBARINFOEX& tbi);
std::ostream& operator<<(std::ostream& os, const TOUCH_HIT_TESTING_INPUT& thti);
std::ostream& operator<<(std::ostream& os, const WINDOWPOS& wp);

namespace Compat
{
	struct WindowMessage
	{
		UINT msg;

		WindowMessage(UINT msg) : msg(msg) {}
	};

	struct WindowMessage16
	{
		UINT16 msg;

		WindowMessage16(UINT16 msg) : msg(msg) {}
	};

	struct WindowMessageStruct
	{
		HWND hwnd;
		WindowMessage msg;
		WPARAM wParam;
		LPARAM lParam;

		WindowMessageStruct(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
			: hwnd(hwnd), msg(msg), wParam(wParam), lParam(lParam)
		{
		}
	};

	std::ostream& operator<<(std::ostream& os, WindowMessage msg);
	std::ostream& operator<<(std::ostream& os, WindowMessage16 msg);
	std::ostream& operator<<(std::ostream& os, WindowMessageStruct wm);
}
