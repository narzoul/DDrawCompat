#include <Common/Hook.h>
#include <Config/Config.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/SettingControl.h>

namespace Overlay
{
	ConfigWindow::ConfigWindow()
		: Window(nullptr, { 0, 0, SettingControl::TOTAL_WIDTH, 200 }, { VK_F11, {} })
	{
		addControl(Config::alternatePixelCenter);
		addControl(Config::antialiasing);
		addControl(Config::displayFilter);
		addControl(Config::renderColorDepth);
		addControl(Config::resolutionScale);
		addControl(Config::textureFilter);
	}

	void ConfigWindow::addControl(Config::Setting& setting)
	{
		const int index = m_controls.size();
		const int rowHeight = 25;

		RECT rect = { 0, index * rowHeight + BORDER / 2, m_rect.right, (index + 1) * rowHeight + BORDER / 2 };
		m_controls.emplace_back(*this, rect, setting);
	}

	RECT ConfigWindow::calculateRect(const RECT& monitorRect) const
	{
		RECT r = { 0, 0, m_rect.right - m_rect.left, m_rect.bottom - m_rect.top };
		OffsetRect(&r, monitorRect.left + (monitorRect.right - monitorRect.left - r.right) / 2,
			monitorRect.top + (monitorRect.bottom - monitorRect.top - r.bottom) / 2);
		return r;
	}
}
