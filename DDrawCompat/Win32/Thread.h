#pragma once

namespace Win32
{
	namespace Thread
	{
		void applyConfig();
		void installHooks();
		void dllThreadAttach();
		void dllThreadDetach();
		void skipWaitingForExclusiveModeMutex(bool skip);
	}
}
