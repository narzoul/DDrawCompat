#include <Common/Hook.h>
#include <Config/Config.h>
#include <Gdi/GuiThread.h>
#include <Input/Input.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/SettingControl.h>

namespace
{
	const int CAPTION_HEIGHT = 22;
}

namespace Overlay
{
	ConfigWindow::ConfigWindow()
		: Window(nullptr, { 0, 0, SettingControl::TOTAL_WIDTH, 330 }, Config::configHotKey.get())
		, m_caption(*this, { 0, 0, SettingControl::TOTAL_WIDTH - CAPTION_HEIGHT + 1, CAPTION_HEIGHT },
			"DDrawCompat Config Overlay", 0, WS_BORDER | WS_VISIBLE)
		, m_closeButton(*this,
			{ SettingControl::TOTAL_WIDTH - CAPTION_HEIGHT, 0, SettingControl::TOTAL_WIDTH, CAPTION_HEIGHT },
			"X", onClose)
		, m_focus(nullptr)
	{
		addControl(Config::alternatePixelCenter);
		addControl(Config::bltFilter);
		addControl(Config::antialiasing);
		addControl(Config::displayFilter);
		addControl(Config::renderColorDepth);
		addControl(Config::resolutionScale);
		addControl(Config::spriteDetection);
		addControl(Config::spriteFilter);
		addControl(Config::spriteTexCoord);
		addControl(Config::textureFilter);
		addControl(Config::vSync);
	}

	void ConfigWindow::addControl(Config::Setting& setting)
	{
		const int index = m_controls.size();
		const int rowHeight = 25;

		RECT rect = { 0, index * rowHeight + BORDER / 2, m_rect.right, (index + 1) * rowHeight + BORDER / 2 };
		OffsetRect(&rect, 0, m_caption.getRect().bottom);
		m_controls.emplace_back(*this, rect, setting);
	}

	RECT ConfigWindow::calculateRect(const RECT& monitorRect) const
	{
		RECT r = { 0, 0, m_rect.right - m_rect.left, m_rect.bottom - m_rect.top };
		OffsetRect(&r, monitorRect.left + (monitorRect.right - monitorRect.left - r.right) / 2,
			monitorRect.top + (monitorRect.bottom - monitorRect.top - r.bottom) / 2);
		return r;
	}

	void ConfigWindow::onClose(Control& control)
	{
		static_cast<ConfigWindow*>(control.getParent())->setVisible(false);
	}

	void ConfigWindow::setFocus(SettingControl* control)
	{
		if (m_focus == control)
		{
			return;
		}
		m_focus = control;

		Input::setCapture(m_focus ? static_cast<Control*>(m_focus) : this);

		if (m_focus)
		{
			RECT r = m_focus->getHighlightRect();
			int sf = getScaleFactor();
			r = { r.left * sf, r.top * sf, r.right * sf, r.bottom * sf };
			Gdi::GuiThread::setWindowRgn(m_hwnd, r);
		}
		else
		{
			Gdi::GuiThread::setWindowRgn(m_hwnd, nullptr);
		}

		invalidate();
	}

	void ConfigWindow::setVisible(bool isVisible)
	{
		if (isVisible != Window::isVisible())
		{
			Window::setVisible(isVisible);
			setFocus(nullptr);
		}
	}
}
