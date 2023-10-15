#pragma once

#include <Windows.h>

namespace Win32
{
	class ScopedDpiAwareness
	{
	public:
		ScopedDpiAwareness(bool dpiAware = true);
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
