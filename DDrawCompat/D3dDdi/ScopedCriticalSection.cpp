#include <Common/Time.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <Gdi/GuiThread.h>
#include <Overlay/StatsWindow.h>

namespace
{
	unsigned g_depth = 0;
	Overlay::StatsWindow* g_statsWindow = nullptr;
}

namespace D3dDdi
{
	Compat::CriticalSection ScopedCriticalSection::s_cs;

	ScopedCriticalSection::ScopedCriticalSection()
		: Compat::ScopedCriticalSection(s_cs)
	{
		if (0 == g_depth)
		{
			g_statsWindow = Gdi::GuiThread::getStatsWindow();
			if (g_statsWindow)
			{
				g_statsWindow->m_ddiUsage.start();
			}
		}
		++g_depth;
	}

	ScopedCriticalSection::~ScopedCriticalSection()
	{
		--g_depth;
		if (0 == g_depth)
		{
			if (g_statsWindow)
			{
				g_statsWindow->m_ddiUsage.stop();
			}
		}
	}
}
