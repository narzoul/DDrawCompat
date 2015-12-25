#pragma once

#define CINTERFACE

#include <fstream>

struct _GUID;
struct tagRECT;

#define LOG_ONCE(msg) \
	static bool isAlreadyLogged##__LINE__ = false; \
	if (!isAlreadyLogged##__LINE__) \
	{ \
		Compat::Log() << msg; \
		isAlreadyLogged##__LINE__ = true; \
	}

inline std::ostream& operator<<(std::ostream& os, const _GUID& guid)
{
	return os << &guid;
}

std::ostream& operator<<(std::ostream& os, tagRECT* rect);

namespace Compat
{
	class Log
	{
	public:
		Log();
		~Log();

		template <typename T>
		Log& operator<<(const T& t)
		{
			s_logFile << t;
			return *this;
		}

	protected:
		template <typename... Params>
		Log(const char* prefix, const char* funcName, Params... params) : Log()
		{
			s_logFile << prefix << ' ' << funcName << '(';
			toList(params...);
			s_logFile << ')';
		}

	private:
		void toList()
		{
		}

		template <typename Param>
		void toList(Param param)
		{
			s_logFile << param;
		}

		template <typename Param, typename... Params>
		void toList(Param firstParam, Params... remainingParams)
		{
			s_logFile << firstParam << ", ";
			toList(remainingParams...);
		}

		static std::ofstream s_logFile;
	};

#ifdef _DEBUG
	class LogEnter : private Log
	{
	public:
		template <typename... Params>
		LogEnter(const char* funcName, Params... params) : Log("-->", funcName, params...)
		{
		}
	};

	class LogLeave : private Log
	{
	public:
		template <typename... Params>
		LogLeave(const char* funcName, Params... params) : Log("<--", funcName, params...)
		{
		}

		template <typename Result>
		void operator<<(const Result& result)
		{
			static_cast<Log&>(*this) << " = " << result;
		}
	};
#else
	class LogEnter
	{
	public:
		template <typename... Params> LogEnter(const char*, Params...) {}
		template <typename Result> void operator<<(const Result&) {}
	};

	typedef LogEnter LogLeave;
#endif
}
