#include <Common/Log.h>

namespace
{
	Compat::CriticalSection g_logCs;
}

std::ostream& operator<<(std::ostream& os, std::nullptr_t)
{
	return os << "null";
}

std::ostream& operator<<(std::ostream& os, const char* str)
{
	if (!str)
	{
		return os << "null";
	}

	if (!Compat::Log::isPointerDereferencingAllowed() || reinterpret_cast<DWORD>(str) <= 0xFFFF)
	{
		return os << static_cast<const void*>(str);
	}

	return os.write(str, strlen(str));
}

std::ostream& operator<<(std::ostream& os, const unsigned char* data)
{
	return os << static_cast<const void*>(data);
}

std::ostream& operator<<(std::ostream& os, const WCHAR* wstr)
{
	if (!wstr)
	{
		return os << "null";
	}

	if (!Compat::Log::isPointerDereferencingAllowed() || reinterpret_cast<DWORD>(wstr) <= 0xFFFF)
	{
		return os << static_cast<const void*>(wstr);
	}

	while (*wstr)
	{
		os.put(static_cast<char>(*wstr));
		++wstr;
	}

	return os;
}

namespace Compat
{
	Log::Log() : m_lock(g_logCs)
	{
		SYSTEMTIME st = {};
		GetLocalTime(&st);

		char header[20];
		sprintf_s(header, "%04hx %02hu:%02hu:%02hu.%03hu ",
			GetCurrentThreadId(), st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
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

	thread_local DWORD Log::s_indent = 0;
	thread_local DWORD Log::s_outParamDepth = 0;
	thread_local bool Log::s_isLeaveLog = false;

	std::ofstream Log::s_logFile("ddraw.log");
}
