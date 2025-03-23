#include <Common/ScopedCriticalSection.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <Gdi/Cursor.h>
#include <Win32/DisplayMode.h>

namespace
{
	const HCURSOR INVALID_CURSOR = static_cast<HCURSOR>(INVALID_HANDLE_VALUE);

	RECT g_clipRect = {};
	HCURSOR g_cursor = INVALID_CURSOR;
	bool g_isEmulated = false;
	RECT g_monitorClipRect = {};
	HCURSOR g_nullCursor = nullptr;
	CURSORINFO g_prevCursorInfo = {};
	Compat::CriticalSection g_cs;

	RECT intersectRect(RECT rect1, RECT rect2);
	void normalizeRect(RECT& rect);

	BOOL WINAPI clipCursor(const RECT* lpRect)
	{
		LOG_FUNC("ClipCursor", lpRect);
		Compat::ScopedCriticalSection lock(g_cs);
		if (IsRectEmpty(&g_monitorClipRect))
		{
			return LOG_RESULT(CALL_ORIG_FUNC(ClipCursor)(lpRect));
		}

		g_clipRect = lpRect ? *lpRect : Win32::DisplayMode::getRealBounds();
		const RECT rect = intersectRect(g_clipRect, g_monitorClipRect);
		CALL_ORIG_FUNC(ClipCursor)(&rect);
		return LOG_RESULT(TRUE);
	}

	BOOL WINAPI getClipCursorInternal(LPRECT lpRect)
	{
		Compat::ScopedCriticalSection lock(g_cs);
		BOOL result = CALL_ORIG_FUNC(GetClipCursor)(lpRect);
		if (result && !IsRectEmpty(&g_monitorClipRect))
		{
			*lpRect = g_clipRect;
		}
		return result;
	}

	BOOL WINAPI getClipCursor(LPRECT lpRect)
	{
		LOG_FUNC("GetClipCursor", lpRect);
		return LOG_RESULT(getClipCursorInternal(lpRect));
	}

	HCURSOR WINAPI getCursor()
	{
		LOG_FUNC("GetCursor");
		Compat::ScopedCriticalSection lock(g_cs);
		auto cursor = CALL_ORIG_FUNC(GetCursor)();
		return LOG_RESULT(cursor == g_nullCursor ? g_cursor : cursor);
	}

	BOOL WINAPI getCursorInfo(PCURSORINFO pci)
	{
		LOG_FUNC("GetCursorInfo", pci);
		Compat::ScopedCriticalSection lock(g_cs);
		BOOL result = CALL_ORIG_FUNC(GetCursorInfo)(pci);
		if (result)
		{
			if (pci->hCursor == g_nullCursor)
			{
				pci->hCursor = g_cursor;
			}
			Gdi::Cursor::clip(pci->ptScreenPos);
		}
		return LOG_RESULT(result);
	}

	RECT intersectRect(RECT rect1, RECT rect2)
	{
		normalizeRect(rect1);
		normalizeRect(rect2);
		IntersectRect(&rect1, &rect1, &rect2);
		return rect1;
	}

	void normalizeRect(RECT& rect)
	{
		if (rect.left == rect.right && rect.top == rect.bottom)
		{
			rect.right++;
			rect.bottom++;
		}
	}

	HCURSOR WINAPI setCursor(HCURSOR hCursor)
	{
		return Gdi::Cursor::setCursor(hCursor);
	}

	void updateClipRect()
	{
		if (!DDraw::RealPrimarySurface::isProcessActive())
		{
			return;
		}

		RECT realClipRect = {};
		CALL_ORIG_FUNC(GetClipCursor)(&realClipRect);

		RECT clipRect = intersectRect(g_clipRect, g_monitorClipRect);
		if (!EqualRect(&clipRect, &realClipRect))
		{
			CALL_ORIG_FUNC(ClipCursor)(&clipRect);
		}
	}
}

namespace Gdi
{
	namespace Cursor
	{
		void clip(POINT& pt)
		{
			RECT r = {};
			if (!getClipCursorInternal(&r))
			{
				return;
			}

			pt.x = std::max(pt.x, r.left);
			pt.x = std::min(pt.x, r.right);
			pt.y = std::max(pt.y, r.top);
			pt.y = std::min(pt.y, r.bottom);
		}

		CURSORINFO getEmulatedCursorInfo()
		{
			CURSORINFO ci = {};
			ci.cbSize = sizeof(ci);

			Compat::ScopedCriticalSection lock(g_cs);
			if (g_isEmulated)
			{
				CALL_ORIG_FUNC(GetCursorInfo)(&ci);
				if (ci.hCursor == g_nullCursor)
				{
					ci.hCursor = g_cursor;
				}
				else
				{
					ci.hCursor = nullptr;
					ci.flags = 0;
				}
				clip(ci.ptScreenPos);
			}

			return ci;
		}

		void installHooks()
		{
			BYTE andPlane = 0xFF;
			BYTE xorPlane = 0;
			g_nullCursor = CreateCursor(nullptr, 0, 0, 1, 1, &andPlane, &xorPlane);

			HOOK_FUNCTION(user32, ClipCursor, clipCursor);
			HOOK_FUNCTION(user32, GetCursor, getCursor);
			HOOK_FUNCTION(user32, GetCursorInfo, getCursorInfo);
			HOOK_FUNCTION(user32, GetClipCursor, getClipCursor);
			HOOK_FUNCTION(user32, SetCursor, ::setCursor);
		}

		bool isEmulated()
		{
			return g_isEmulated;
		}

		HCURSOR setCursor(HCURSOR cursor)
		{
			LOG_FUNC("SetCursor", cursor);
			Compat::ScopedCriticalSection lock(g_cs);
			if (cursor == g_nullCursor)
			{
				return LOG_RESULT(g_cursor);
			}

			HCURSOR prevCursor = g_cursor != INVALID_CURSOR ? g_cursor : CALL_ORIG_FUNC(GetCursor)();
			g_cursor = cursor;
			CALL_ORIG_FUNC(SetCursor)(g_isEmulated && cursor ? g_nullCursor : cursor);
			return LOG_RESULT(prevCursor);
		}

		void setEmulated(bool isEmulated)
		{
			Compat::ScopedCriticalSection lock(g_cs);
			if (isEmulated == g_isEmulated)
			{
				return;
			}

			LOG_DEBUG << "Cursor::setEmulated: " << isEmulated;
			g_isEmulated = isEmulated;
			g_prevCursorInfo = {};

			POINT pos = {};
			CALL_ORIG_FUNC(GetCursorPos)(&pos);
			SetCursorPos(pos.x, pos.y);
		}

		void setMonitorClipRect(const RECT& rect)
		{
			if (EqualRect(&rect, &g_monitorClipRect))
			{
				return;
			}

			LOG_DEBUG << "Cursor::setMonitorClipRect: " << rect;
			Compat::ScopedCriticalSection lock(g_cs);
			if (IsRectEmpty(&rect))
			{
				if (!IsRectEmpty(&g_monitorClipRect))
				{
					if (DDraw::RealPrimarySurface::isProcessActive())
					{
						CALL_ORIG_FUNC(ClipCursor)(&g_clipRect);
					}
					g_clipRect = {};
					g_monitorClipRect = {};
				}
			}
			else
			{
				if (IsRectEmpty(&g_monitorClipRect))
				{
					CALL_ORIG_FUNC(GetClipCursor)(&g_clipRect);
				}
				g_monitorClipRect = rect;
				updateClipRect();
			}
		}

		void update()
		{
			Compat::ScopedCriticalSection lock(g_cs);
			if (!IsRectEmpty(&g_monitorClipRect))
			{
				updateClipRect();
			}

			if (g_isEmulated)
			{
				CURSORINFO cursorInfo = getEmulatedCursorInfo();
				if ((CURSOR_SHOWING == cursorInfo.flags) != (CURSOR_SHOWING == g_prevCursorInfo.flags) ||
					CURSOR_SHOWING == cursorInfo.flags &&
					(cursorInfo.hCursor != g_prevCursorInfo.hCursor || cursorInfo.ptScreenPos != g_prevCursorInfo.ptScreenPos))
				{
					g_prevCursorInfo = cursorInfo;
					DDraw::RealPrimarySurface::scheduleOverlayUpdate();
				}
			}
		}
	}
}
