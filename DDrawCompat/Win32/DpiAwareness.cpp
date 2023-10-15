#include <ShellScalingApi.h>

#include <Common/Log.h>
#include <Config/Settings/DpiAwareness.h>
#include <Win32/DpiAwareness.h>

namespace
{
	decltype(&AreDpiAwarenessContextsEqual) g_areDpiAwarenessContextsEqual = nullptr;
	decltype(&GetProcessDpiAwareness) g_getProcessDpiAwareness = nullptr;
	decltype(&GetThreadDpiAwarenessContext) g_getThreadDpiAwarenessContext = nullptr;
	decltype(&IsValidDpiAwarenessContext) g_isValidDpiAwarenessContext = nullptr;
	decltype(&SetProcessDpiAwareness) g_setProcessDpiAwareness = nullptr;
	decltype(&SetProcessDpiAwarenessContext) g_setProcessDpiAwarenessContext = nullptr;
	decltype(&SetThreadDpiAwarenessContext) g_setThreadDpiAwarenessContext = nullptr;

	void logDpiAwareness(bool isSuccessful, DPI_AWARENESS_CONTEXT dpiAwareness, const char* funcName)
	{
		LOG_INFO << (isSuccessful ? "DPI awareness was successfully changed" : "Failed to change DPI awareness")
			<< " to \"" << Config::dpiAwareness.convertToString(dpiAwareness) << "\" via " << funcName;
	}
}

namespace Win32
{
	ScopedDpiAwareness::ScopedDpiAwareness(bool dpiAware)
		: m_prevContext(dpiAware ? DpiAwareness::setThreadContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) : nullptr)
	{
	}

	ScopedDpiAwareness::~ScopedDpiAwareness()
	{
		if (m_prevContext)
		{
			DpiAwareness::setThreadContext(m_prevContext);
		}
	}

	namespace DpiAwareness
	{
		DPI_AWARENESS_CONTEXT getThreadContext()
		{
			if (g_getThreadDpiAwarenessContext && g_areDpiAwarenessContextsEqual)
			{
				auto context = g_getThreadDpiAwarenessContext();
				if (g_areDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_UNAWARE))
				{
					return DPI_AWARENESS_CONTEXT_UNAWARE;
				}
				else if (g_areDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_SYSTEM_AWARE))
				{
					return DPI_AWARENESS_CONTEXT_SYSTEM_AWARE;
				}
				else if (g_areDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE))
				{
					return DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE;
				}
				else if (g_areDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
				{
					return DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;
				}
				else if (g_areDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED))
				{
					return DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED;
				}
			}

			if (g_getProcessDpiAwareness)
			{
				PROCESS_DPI_AWARENESS awareness = PROCESS_DPI_UNAWARE;
				if (SUCCEEDED(g_getProcessDpiAwareness(nullptr, &awareness)))
				{
					switch (awareness)
					{
					case PROCESS_DPI_UNAWARE:
						return DPI_AWARENESS_CONTEXT_UNAWARE;
					case PROCESS_SYSTEM_DPI_AWARE:
						return DPI_AWARENESS_CONTEXT_SYSTEM_AWARE;
					case PROCESS_PER_MONITOR_DPI_AWARE:
						return DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE;
					}
				}

			}

			return IsProcessDPIAware() ? DPI_AWARENESS_CONTEXT_SYSTEM_AWARE : DPI_AWARENESS_CONTEXT_UNAWARE;
		}

		void init()
		{
			auto user32 = GetModuleHandle("user32");
			auto shcore = LoadLibrary("shcore");

			g_areDpiAwarenessContextsEqual = reinterpret_cast<decltype(&AreDpiAwarenessContextsEqual)>(
				GetProcAddress(user32, "AreDpiAwarenessContextsEqual"));
			g_getProcessDpiAwareness = reinterpret_cast<decltype(&GetProcessDpiAwareness)>(
				GetProcAddress(shcore, "GetProcessDpiAwareness"));
			g_getThreadDpiAwarenessContext = reinterpret_cast<decltype(&GetThreadDpiAwarenessContext)>(
				GetProcAddress(user32, "GetThreadDpiAwarenessContext"));
			g_isValidDpiAwarenessContext = reinterpret_cast<decltype(&IsValidDpiAwarenessContext)>(
				GetProcAddress(user32, "IsValidDpiAwarenessContext"));
			g_setProcessDpiAwareness = reinterpret_cast<decltype(&SetProcessDpiAwareness)>(
				GetProcAddress(shcore, "SetProcessDpiAwareness"));
			g_setProcessDpiAwarenessContext = reinterpret_cast<decltype(&SetProcessDpiAwarenessContext)>(
				GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
			g_setThreadDpiAwarenessContext = reinterpret_cast<decltype(&SetThreadDpiAwarenessContext)>(
				GetProcAddress(user32, "SetThreadDpiAwarenessContext"));

			LOG_INFO << "Initial DPI awareness: " << Config::dpiAwareness.convertToString(getThreadContext());

			auto dpiAwareness = Config::dpiAwareness.get();
			if (!dpiAwareness)
			{
				return;
			}

			if (g_isValidDpiAwarenessContext && g_setProcessDpiAwarenessContext)
			{
				if (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == dpiAwareness &&
					!g_isValidDpiAwarenessContext(dpiAwareness))
				{
					dpiAwareness = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE;
				}

				if (DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED == dpiAwareness &&
					!g_isValidDpiAwarenessContext(dpiAwareness))
				{
					dpiAwareness = DPI_AWARENESS_CONTEXT_UNAWARE;
				}

				logDpiAwareness(g_setProcessDpiAwarenessContext(dpiAwareness), dpiAwareness, "SetProcessDpiAwarenessContext");
				return;
			}

			if (g_setProcessDpiAwareness)
			{
				HRESULT result = S_OK;
				if (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE == dpiAwareness ||
					DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == dpiAwareness)
				{
					dpiAwareness = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE;
					result = g_setProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
				}
				else if (DPI_AWARENESS_CONTEXT_SYSTEM_AWARE == dpiAwareness)
				{
					result = g_setProcessDpiAwareness(PROCESS_SYSTEM_DPI_AWARE);
				}
				else
				{
					dpiAwareness = DPI_AWARENESS_CONTEXT_UNAWARE;
					result = g_setProcessDpiAwareness(PROCESS_DPI_UNAWARE);
				}

				logDpiAwareness(SUCCEEDED(result), dpiAwareness, "SetProcessDpiAwareness");
				return;
			}

			if (DPI_AWARENESS_CONTEXT_UNAWARE == dpiAwareness ||
				DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED == dpiAwareness)
			{
				LOG_INFO << "DPI awareness was not changed";
			}
			else
			{
				logDpiAwareness(SetProcessDPIAware(), DPI_AWARENESS_CONTEXT_SYSTEM_AWARE, "SetProcessDPIAware");
			}
		}

		bool isMixedModeSupported()
		{
			return g_setThreadDpiAwarenessContext;
		}

		DPI_AWARENESS_CONTEXT setThreadContext(DPI_AWARENESS_CONTEXT context)
		{
			return g_setThreadDpiAwarenessContext ? g_setThreadDpiAwarenessContext(context) : nullptr;
		}
	}
}
