#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

#include "DDrawLog.h"

std::ostream& operator<<(std::ostream& os, LPRECT rect)
{
	if (rect)
	{
		os << "RECT(" << rect->left << ',' << rect->top << ',' << rect->right << ',' << rect->bottom << ')';
	}
	else
	{
		os << "NullRect";
	}
	return os;
}

namespace Compat
{
	Log::Log()
	{
		SYSTEMTIME st = {};
		GetLocalTime(&st);

		char time[100];
		sprintf_s(time, "%02hu:%02hu:%02hu.%03hu ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

		s_logFile << GetCurrentThreadId() << " " << time;
	}

	Log::~Log()
	{
		s_logFile << std::endl;
	}

	std::ofstream Log::s_logFile("ddraw.log");
}
