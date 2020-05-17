#pragma once

#include <fstream>
#include <functional>
#include <ostream>
#include <string>
#include <type_traits>

#include <Windows.h>

#include <Common/ScopedCriticalSection.h>
#include <DDraw/Log.h>
#include <Win32/Log.h>

#ifdef DEBUGLOGS
#define LOG_DEBUG Compat::Log()
#define LOG_FUNC(...) Compat::LogFunc logFunc(__VA_ARGS__)
#define LOG_RESULT(...) logFunc.setResult(__VA_ARGS__)
#else
#define LOG_DEBUG if (false) Compat::Log()
#define LOG_FUNC(...)
#define LOG_RESULT(...) __VA_ARGS__
#endif

#define LOG_ONCE(msg) \
	{ \
		static bool isAlreadyLogged = false; \
		if (!isAlreadyLogged) \
		{ \
			Compat::Log() << msg; \
			isAlreadyLogged = true; \
		} \
	}

#define LOG_CONST_CASE(constant) case constant: return os << #constant;

std::ostream& operator<<(std::ostream& os, std::nullptr_t);
std::ostream& operator<<(std::ostream& os, const char* str);
std::ostream& operator<<(std::ostream& os, const unsigned char* data);
std::ostream& operator<<(std::ostream& os, const WCHAR* wstr);

namespace Compat
{
	using ::operator<<;

	namespace detail
	{
		template <typename T>
		struct Hex
		{
			explicit Hex(T val) : val(val) {}
			T val;
		};

		template <typename Elem>
		struct Array
		{
			Array(const Elem* elem, const unsigned long size) : elem(elem), size(size) {}
			const Elem* elem;
			const unsigned long size;
		};

		template <typename T>
		struct Out
		{
			explicit Out(T val) : val(val) {}
			T val;
		};

		class LogParams;

		class LogFirstParam
		{
		public:
			LogFirstParam(std::ostream& os) : m_os(os) {}
			template <typename T> LogParams operator<<(const T& val) { m_os << val; return LogParams(m_os); }

		protected:
			std::ostream& m_os;
		};

		class LogParams
		{
		public:
			LogParams(std::ostream& os) : m_os(os) {}
			template <typename T> LogParams& operator<<(const T& val) { m_os << ',' << val; return *this; }

			operator std::ostream&() { return m_os; }

		private:
			std::ostream& m_os;
		};

		template <typename T>
		std::ostream& operator<<(std::ostream& os, Hex<T> hex)
		{
			os << "0x" << std::hex << hex.val << std::dec;
			return os;
		}

		template <typename Elem>
		std::ostream& operator<<(std::ostream& os, Array<Elem> array)
		{
			os << '[';
			if (Log::isPointerDereferencingAllowed())
			{
				if (0 != array.size)
				{
					os << array.elem[0];
				}
				for (unsigned long i = 1; i < array.size; ++i)
				{
					os << ',' << array.elem[i];
				}
			}
			return os << ']';
		}

		template <typename T>
		std::ostream& operator<<(std::ostream& os, Out<T> out)
		{
			++Log::s_outParamDepth;
			os << out.val;
			--Log::s_outParamDepth;
			return os;
		}
	}

	template <typename T> detail::Hex<T> hex(T val)
	{
		return detail::Hex<T>(val);
	}

	template <typename Elem>
	detail::Array<Elem> array(const Elem* elem, const unsigned long size)
	{
		return detail::Array<Elem>(elem, size);
	}

	template <typename T> detail::Out<T> out(const T& val)
	{
		return detail::Out<T>(val);
	}

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

		static void initLogging(std::string processName);
		static bool isPointerDereferencingAllowed() { return s_isLeaveLog || 0 == s_outParamDepth; }

	protected:
		template <typename... Params>
		Log(const char* prefix, const char* funcName, Params... params) : Log()
		{
			s_logFile << prefix << ' ' << funcName << '(';
			toList(params...);
			s_logFile << ')';
		}

	private:
		friend class LogFunc;
		template <typename T> friend std::ostream& detail::operator<<(std::ostream& os, detail::Out<T> out);

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

		ScopedCriticalSection m_lock;

		static thread_local DWORD s_indent;
		static thread_local DWORD s_outParamDepth;
		static thread_local bool s_isLeaveLog;

		static std::ofstream s_logFile;
	};

	class LogFunc
	{
	public:
		template <typename... Params>
		LogFunc(const char* funcName, Params... params)
			: m_printCall([=](Log& log) { log << funcName << '('; log.toList(params...); log << ')'; })
		{
			Log log;
			log << "> ";
			m_printCall(log);
			Log::s_indent += 2;
		}

		~LogFunc()
		{
			Log::s_isLeaveLog = true;
			Log::s_indent -= 2;
			Log log;
			log << "< ";
			m_printCall(log);

			if (m_printResult)
			{
				log << " = ";
				m_printResult(log);
			}
			Log::s_isLeaveLog = false;
		}

		template <typename T>
		T setResult(T result)
		{
			m_printResult = [=](Log& log) { log << std::hex << result << std::dec; };
			return result;
		}

	private:
		std::function<void(Log&)> m_printCall;
		std::function<void(Log&)> m_printResult;
	};

	class LogStruct : public detail::LogFirstParam
	{
	public:
		LogStruct(std::ostream& os) : detail::LogFirstParam(os) { m_os << '{'; }
		~LogStruct() { m_os << '}'; }
	};
}

template <typename T>
typename std::enable_if<std::is_class<T>::value && !std::is_same<T, std::string>::value, std::ostream&>::type
operator<<(std::ostream& os, const T& t)
{
	return os << static_cast<const void*>(&t);
}

template <typename T>
typename std::enable_if<std::is_class<T>::value, std::ostream&>::type
operator<<(std::ostream& os, T* t)
{
	if (!t)
	{
		return os << "null";
	}

	if (!Compat::Log::isPointerDereferencingAllowed() || reinterpret_cast<DWORD>(t) <= 0xFFFF)
	{
		return os << static_cast<const void*>(t);
	}

	return os << *t;
}

template <typename T>
std::ostream& operator<<(std::ostream& os, T** t)
{
	if (!t)
	{
		return os << "null";
	}

	os << static_cast<const void*>(t);

	if (!Compat::Log::isPointerDereferencingAllowed() || reinterpret_cast<DWORD>(t) <= 0xFFFF)
	{
		return os;
	}

	return os << '=' << *t;
}
