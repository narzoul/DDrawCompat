#include <regex>
#include <sstream>
#include <string>

#include <Common/Log.h>
#include <Common/Path.h>
#include <Common/ScopedCriticalSection.h>

namespace
{
	Compat::CriticalSection g_logCs;
	std::ofstream g_logFile;
	std::ostringstream g_initialLogStream;
}

namespace Compat
{
	std::string getTrimmedTypeName(const std::string& typeName)
	{
		std::string prefix("struct ");
		if (prefix == typeName.substr(0, prefix.length()))
		{
			return typeName.substr(prefix.length());
		}
		return typeName;
	}

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

	Log::Log(unsigned logLevel) : m_isEnabled(logLevel <= s_logLevel)
	{
		if (!m_isEnabled)
		{
			return;
		}

		EnterCriticalSection(&g_logCs);
		SYSTEMTIME st = {};
		GetLocalTime(&st);

		char header[20];
		if (s_logLevel >= Config::Settings::LogLevel::DEBUG)
		{
			sprintf_s(header, "%04hx %02hu:%02hu:%02hu.%03hu ",
				GetCurrentThreadId(), st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
		}
		else
		{
			sprintf_s(header, "%02hu:%02hu:%02hu.%03hu ",
				st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
		}
		*s_logStream << header;

		if (0 != s_indent)
		{
			std::fill_n(std::ostreambuf_iterator<char>(*s_logStream), s_indent, ' ');
		}
	}

	Log::~Log()
	{
		if (m_isEnabled)
		{
			*s_logStream << std::endl;
			LeaveCriticalSection(&g_logCs);
		}
	}

	void Log::initLogging(std::filesystem::path processPath, unsigned logLevel)
	{
		s_logLevel = logLevel;
		s_logStream = &g_logFile;

		if (Config::Settings::LogLevel::NONE == logLevel)
		{
			g_initialLogStream = {};
			return;
		}

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

			g_logFile.open(logFilePath, std::ios_base::out, SH_DENYWR);
			if (!g_logFile.fail())
			{
				std::string initialLogs(g_initialLogStream.str());
				if (logLevel < Config::Settings::LogLevel::DEBUG)
				{
					initialLogs = std::regex_replace(initialLogs, std::regex("^....."), "");
				}

				g_logFile.write(initialLogs.c_str(), initialLogs.size());
				g_initialLogStream = {};
				return;
			}
		}

		s_logLevel = Config::Settings::LogLevel::NONE;
	}

	LogFuncBase::~LogFuncBase()
	{
		if (m_isEnabled)
		{
			Log::s_isLeaveLog = true;
			Log::s_indent -= 2;
			Log log(Config::Settings::LogLevel::DEBUG);
			log << "< ";
			logCall(log);

			if (m_logResult)
			{
				log << " = ";
				m_logResult(log);
			}
			Log::s_isLeaveLog = false;
		}
	}

	thread_local DWORD Log::s_indent = 0;

	bool Log::s_isLeaveLog = false;
	unsigned Log::s_logLevel = Config::Settings::LogLevel::DEBUG;
	std::ostream* Log::s_logStream = &g_initialLogStream;
}
