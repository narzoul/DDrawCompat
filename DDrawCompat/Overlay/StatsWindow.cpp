#include <array>
#include <functional>

#include <Common/Time.h>
#include <Config/Settings/StatsHotKey.h>
#include <Config/Settings/StatsRows.h>
#include <Gdi/GuiThread.h>
#include <Input/Input.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/StatsWindow.h>

namespace
{
	class UpdateStats
	{
	public:
		UpdateStats(StatsQueue& statsQueue)
			: m_statsQueue(statsQueue)
		{
		}

		std::array<std::string, 4> operator()(StatsQueue::TickCount tickCount) const
		{
			auto stats = m_statsQueue.getStats(tickCount);
			return { toString(stats.cur), toString(stats.avg), toString(stats.min), toString(stats.max) };
		}

	private:
		std::string toString(double stat) const
		{
			if (std::isnan(stat))
			{
				return "-";
			}
			
			const char unitLetter[] = { 0, 'k', 'm', 'b' };
			std::size_t unitIndex = 0;
			while (stat >= 1000 && unitIndex + 1 < sizeof(unitLetter))
			{
				++unitIndex;
				stat /= 1000;
			}

			char buf[20] = {};
			if (0 == unitIndex)
			{
				snprintf(buf, sizeof(buf), "%.0f", stat);
				return buf;
			}

			auto len = snprintf(buf, sizeof(buf), "%.2f", stat);
			const auto decimalPoint = strchr(buf, '.');
			const auto intLen = decimalPoint ? decimalPoint - buf : len;
			if (len > 4)
			{
				len = intLen >= 3 ? intLen : 4;
			}
			buf[len] = unitLetter[unitIndex];
			buf[len + 1] = 0;
			return buf;
		}

		StatsQueue& m_statsQueue;
	};

	const int ROW_HEIGHT = 15;
}

namespace Overlay
{
	StatsWindow::StatsWindow()
		: Window(nullptr,
			{ 0, 0, StatsControl::getWidth(), static_cast<int>(Config::statsRows.get().size()) * ROW_HEIGHT + BORDER },
			0, Config::statsHotKey.get())
	{
		m_statsRows.push_back({ "", [](auto) { return std::array<std::string, 4>{ "cur", "avg", "min", "max" }; },
			WS_VISIBLE | WS_DISABLED });
		m_statsRows.push_back({ "Present count", UpdateStats(m_present.m_count) });
		m_statsRows.push_back({ "Present rate", UpdateStats(m_present.m_rate) });
		m_statsRows.push_back({ "Present time", UpdateStats(m_present.m_time) });
		m_statsRows.push_back({ "Flip count", UpdateStats(m_flip.m_count) });
		m_statsRows.push_back({ "Flip rate", UpdateStats(m_flip.m_rate) });
		m_statsRows.push_back({ "Flip time", UpdateStats(m_flip.m_time) });
		m_statsRows.push_back({ "Blit count", UpdateStats(m_blit.m_count) });
		m_statsRows.push_back({ "Blit rate", UpdateStats(m_blit.m_rate) });
		m_statsRows.push_back({ "Blit time", UpdateStats(m_blit.m_time) });
		m_statsRows.push_back({ "Lock count", UpdateStats(m_lock.m_count) });
		m_statsRows.push_back({ "Lock rate", UpdateStats(m_lock.m_rate) });
		m_statsRows.push_back({ "Lock time", UpdateStats(m_lock.m_time) });
		m_statsRows.push_back({ "DDI usage", UpdateStats(m_ddiUsage) });
		m_statsRows.push_back({ "GDI objects", UpdateStats(m_gdiObjects) });

		for (auto statsRowIndex : Config::statsRows.get())
		{
			auto& statsRow = m_statsRows[statsRowIndex];
			auto& statsControl = addControl(statsRow.name, statsRow.updateFunc, statsRow.style);
			if (statsRow.style & WS_DISABLED)
			{
				statsControl.update(0);
			}
		}
	}

	StatsControl& StatsWindow::addControl(const std::string& name, StatsControl::UpdateFunc updateFunc, DWORD style)
	{
		const int index = m_statsControls.size();
		RECT rect = { 0, index * ROW_HEIGHT + BORDER / 2, m_rect.right, (index + 1) * ROW_HEIGHT + BORDER / 2 };
		return m_statsControls.emplace_back(*this, rect, name, updateFunc, style);
	}

	RECT StatsWindow::calculateRect(const RECT& monitorRect) const
	{
		RECT r = { 0, 0, m_rect.right - m_rect.left, m_rect.bottom - m_rect.top };
		OffsetRect(&r, monitorRect.left + monitorRect.right - r.right, monitorRect.top);
		return r;
	}

	HWND StatsWindow::getTopmost() const
	{
		auto configWindow = Gdi::GuiThread::getConfigWindow();
		return (configWindow && configWindow->isVisible()) ? configWindow->getWindow() : Window::getTopmost();
	}

	void StatsWindow::updateStats()
	{
		static auto prevTickCount = StatsQueue::getTickCount() - 1;
		m_tickCount = StatsQueue::getTickCount();
		if (m_tickCount == prevTickCount)
		{
			return;
		}

		m_gdiObjects.addSample(m_tickCount, GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
		for (auto& statsControl : m_statsControls)
		{
			if (statsControl.isEnabled())
			{
				statsControl.update(m_tickCount);
			}
		}
		prevTickCount = m_tickCount;
	}
}
