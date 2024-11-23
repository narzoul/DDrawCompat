#pragma once

namespace Win32
{
	namespace DisplayMode
	{
		struct MonitorInfo;
	}
}

namespace Gdi
{
	namespace DcFunctions
	{
		void disableDibRedirection(bool disable);
		void setFullscreenMonitorInfo(const Win32::DisplayMode::MonitorInfo& mi);

		void installHooks();
	}
}
