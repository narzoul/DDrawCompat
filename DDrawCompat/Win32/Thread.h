#pragma once

namespace Win32
{
	namespace Thread
	{
		void applyConfig();
		void installHooks();
		void rotateCpuAffinity();
		void skipWaitingForExclusiveModeMutex(bool skip);
	}
}
