#pragma once

#include <Windows.h>

namespace Win32
{
	class ScopedDpiAwareness
	{
	public:
		ScopedDpiAwareness(DPI_AWARENESS_CONTEXT context = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		~ScopedDpiAwareness();

	private:
		DPI_AWARENESS_CONTEXT m_prevContext;
	};

	namespace DpiAwareness
	{
		void init();

		bool isMixedModeSupported();

		DPI_AWARENESS_CONTEXT getThreadContext();
		DPI_AWARENESS_CONTEXT setThreadContext(DPI_AWARENESS_CONTEXT context);
	}
}
