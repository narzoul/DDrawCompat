#include "CompatGdi.h"
#include "CompatGdiTitleBar.h"

namespace
{
	enum TitleBarInfoIndex
	{
		TBII_TITLEBAR = 0,
		TBII_MINIMIZE_BUTTON = 2,
		TBII_MAXIMIZE_BUTTON = 3,
		TBII_HELP_BUTTON = 4,
		TBII_CLOSE_BUTTON = 5
	};
}

namespace CompatGdi
{
	TitleBar::TitleBar(HWND hwnd, HDC compatDc) :
		m_hwnd(hwnd), m_compatDc(compatDc), m_buttonWidth(0), m_buttonHeight(0), m_tbi(),
		m_hasTitleBar(false), m_isToolWindow(false)
	{
		m_hasTitleBar = 0 != (GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CAPTION);
		if (!m_hasTitleBar)
		{
			return;
		}

		m_tbi.cbSize = sizeof(m_tbi);
		SendMessage(hwnd, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&m_tbi));
		m_hasTitleBar = !IsRectEmpty(&m_tbi.rcTitleBar);
		if (!m_hasTitleBar)
		{
			return;
		}

		POINT origin = {};
		ClientToScreen(hwnd, &origin);
		m_tbi.rcTitleBar.left = origin.x;

		RECT windowRect = {};
		GetWindowRect(hwnd, &windowRect);
		OffsetRect(&m_tbi.rcTitleBar, -windowRect.left, -windowRect.top);

		m_isToolWindow = 0 != (GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW);
		m_buttonWidth = GetSystemMetrics(m_isToolWindow ? SM_CXSMSIZE : SM_CXSIZE) - 2;
		m_buttonHeight = GetSystemMetrics(m_isToolWindow ? SM_CYSMSIZE : SM_CYSIZE) - 4;

		for (std::size_t i = TBII_MINIMIZE_BUTTON; i <= TBII_CLOSE_BUTTON; ++i)
		{
			if (isVisible(i))
			{
				OffsetRect(&m_tbi.rgrect[i], -windowRect.left, -windowRect.top);
				adjustButtonSize(m_tbi.rgrect[i]);
			}
		}
	}

	void TitleBar::adjustButtonSize(RECT& rect) const
	{
		rect.left += (rect.right - rect.left - m_buttonWidth) / 2;
		rect.top += (rect.bottom - rect.top - m_buttonHeight) / 2;
		rect.right = rect.left + m_buttonWidth;
		rect.bottom = rect.top + m_buttonHeight;
	}

	void TitleBar::drawAll() const
	{
		drawCaption();
		drawButtons();
	}

	void TitleBar::drawButtons() const
	{
		if (!m_hasTitleBar)
		{
			return;
		}

		drawButton(TBII_MINIMIZE_BUTTON, DFCS_CAPTIONMIN);
		drawButton(TBII_MAXIMIZE_BUTTON, IsZoomed(m_hwnd) ? DFCS_CAPTIONRESTORE : DFCS_CAPTIONMAX);
		drawButton(TBII_HELP_BUTTON, DFCS_CAPTIONHELP);
		drawButton(TBII_CLOSE_BUTTON, DFCS_CAPTIONCLOSE);
	}

	void TitleBar::drawCaption() const
	{
		if (!m_hasTitleBar)
		{
			return;
		}

		UINT flags = DC_ICON | DC_TEXT;
		if (GetActiveWindow() == m_hwnd)
		{
			flags |= DC_ACTIVE;
		}
		if (m_isToolWindow)
		{
			flags |= DC_SMALLCAP;
		}

		CALL_ORIG_GDI(DrawCaption)(m_hwnd, m_compatDc, &m_tbi.rcTitleBar, flags);
	}

	void TitleBar::drawButton(std::size_t tbiIndex, UINT dfcState) const
	{
		if (!isVisible(tbiIndex))
		{
			return;
		}

		DWORD stateFlags = 0;
		if (m_tbi.rgstate[tbiIndex] & STATE_SYSTEM_UNAVAILABLE)
		{
			stateFlags |= DFCS_INACTIVE;
		}
		else if (m_tbi.rgstate[tbiIndex] & STATE_SYSTEM_PRESSED)
		{
			stateFlags |= DFCS_PUSHED;
		}

		RECT rect = m_tbi.rgrect[tbiIndex];
		CALL_ORIG_GDI(DrawFrameControl)(m_compatDc, &rect, DFC_CAPTION, dfcState | stateFlags);
	}

	void TitleBar::excludeFromClipRegion() const
	{
		if (m_hasTitleBar)
		{
			const RECT& r = m_tbi.rcTitleBar;
			ExcludeClipRect(m_compatDc, r.left, r.top, r.right, r.bottom);
		}
	}

	bool TitleBar::isVisible(std::size_t tbiIndex) const
	{
		return !(m_tbi.rgstate[tbiIndex] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN));
	}
}
