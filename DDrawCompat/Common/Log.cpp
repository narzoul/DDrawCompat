#include <string>

#include <Common/Log.h>
#include <Common/Path.h>

namespace
{
	Compat::CriticalSection g_logCs;
}

namespace Compat
{
	LogStream operator<<(LogStream os, const void* ptr)
	{
		if (ptr)
		{
			os.getStream() << '&' << static_cast<const void*>(ptr);
		}
		else
		{
			os.getStream() << "null";
		}
		return os;
	}

	Log::Log() : m_lock(g_logCs)
	{
		SYSTEMTIME st = {};
		GetLocalTime(&st);

		char header[20];
#ifdef DEBUGLOGS
		sprintf_s(header, "%04hx %02hu:%02hu:%02hu.%03hu ",
			GetCurrentThreadId(), st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
#else
		sprintf_s(header, "%02hu:%02hu:%02hu.%03hu ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
#endif
		s_logFile << header;

		if (0 != s_indent)
		{
			std::fill_n(std::ostreambuf_iterator<char>(s_logFile), s_indent, ' ');
		}
	}

	Log::~Log()
	{
		s_logFile << std::endl;
	}

	void Log::initLogging(std::filesystem::path processPath)
	{
		if (Compat::isEqual(processPath.extension(), ".exe"))
		{
			processPath.replace_extension();
		}
		processPath.replace_filename(L"DDrawCompat-" + processPath.filename().native());

		for (int i = 1; i < 100; ++i)
		{
			auto logFilePath(processPath);
			if (i > 1)
			{
				logFilePath += '[' + std::to_string(i) + ']';
			}
			logFilePath += ".log";

			s_logFile.open(logFilePath, std::ios_base::out, SH_DENYWR);
			if (!s_logFile.fail())
			{
				return;
			}
		}
	}

	thread_local DWORD Log::s_indent = 0;

	bool Log::s_isLeaveLog = false;
	std::ofstream Log::s_logFile;
}
