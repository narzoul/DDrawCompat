#pragma once

#include <filesystem>
#include <fstream>
#include <functional>
#include <ostream>
#include <tuple>
#include <type_traits>
#include <utility>

#include <Windows.h>

#include <Common/ScopedCriticalSection.h>
#include <DDraw/Log.h>
#include <Win32/Log.h>

#ifdef DEBUGLOGS
#define LOG_DEBUG Compat::Log()
#define LOG_FUNC(...) Compat::LogFunc logFunc(__VA_ARGS__)
#define LOG_FUNC_CUSTOM(funcPtr, ...) Compat::LogFunc<funcPtr> logFunc(__VA_ARGS__)
#define LOG_RESULT(...) logFunc.setResult(__VA_ARGS__)
#else
#define LOG_DEBUG if constexpr (false) Compat::Log()
#define LOG_FUNC(...)
#define LOG_FUNC_CUSTOM(funcPtr, ...)
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

namespace Compat
{
	template <typename T, typename = void>
	struct IsPrintable : std::false_type
	{
	};

	template <typename T>
	struct IsPrintable<T, decltype(std::declval<std::ostream&>() << std::declval<T>(), void())> : std::true_type
	{
	};

	template<typename T>
	struct IsString : std::disjunction<
		std::is_same<char*, std::decay_t<T>>,
		std::is_same<const char*, std::decay_t<T>>,
		std::is_same<wchar_t*, std::decay_t<T>>,
		std::is_same<const wchar_t*, std::decay_t<T>>,
		std::is_same<std::string, std::decay_t<T>>,
		std::is_same<std::wstring, std::decay_t<T>>>
	{
	};

	class LogStream
	{
	public:
		explicit LogStream(std::ostream& os) : m_os(os) {}

		std::ostream& getStream() const { return m_os; }
		operator std::ostream& () const { return m_os; }

	private:
		std::ostream& m_os;
	};

	LogStream operator<<(LogStream os, const void* ptr);

	template <typename T>
	std::enable_if_t<IsPrintable<const T&>::value || std::is_class_v<T>, LogStream> operator<<(LogStream os, const T& val)
	{
		if constexpr (IsPrintable<const T&>::value)
		{
			os.getStream() << val;
		}
		else
		{
			os << static_cast<const void*>(&val);
		}
		return os;
	}

	template <typename T>
	LogStream operator<<(LogStream os, const T* ptr)
	{
		if constexpr (std::is_function_v<T>)
		{
			os.getStream() << ptr;
		}
		else if (!ptr)
		{
			os.getStream() << "null";
		}
		else if (reinterpret_cast<DWORD>(ptr) <= 0xFFFF)
		{
			os << static_cast<const void*>(ptr);
		}
		else if constexpr (std::is_same_v<T, char> || std::is_same_v<T, wchar_t>)
		{
			while (*ptr)
			{
				os.getStream().put(static_cast<char>(*ptr));
				++ptr;
			}
		}
		else if constexpr (std::is_pointer_v<T> || IsPrintable<const T&>::value)
		{
			os << '*' << *ptr;
		}
		else
		{
			os << *ptr;
		}

		return os;
	}

	template <typename T>
	LogStream operator<<(LogStream os, T* ptr)
	{
		return os << static_cast<const T*>(ptr);
	}

	template <typename T1, typename T2>
	LogStream operator<<(LogStream os, const std::pair<T1, T2>& pair)
	{
		return Compat::LogStruct(os)
			<< pair.first
			<< pair.second;
	}

	template <typename Elem>
	struct Array
	{
		Array(const Elem* elem, const unsigned long size) : elem(elem), size(size) {}
		const Elem* elem;
		const unsigned long size;
	};

	template <typename T>
	struct Hex
	{
		explicit Hex(T val) : val(val) {}
		T val;
	};

	struct HexByte
	{
		explicit HexByte(BYTE val) : val(val) {}
		BYTE val;
	};

	template <typename T>
	struct Out
	{
		explicit Out(const T& val) : val(val) {}
		const T& val;
	};

	template <typename Elem>
	Array<Elem> array(const Elem* elem, const unsigned long size)
	{
		return Array<Elem>(elem, size);
	}

	template <typename T>
	const void* getOutPtr(const T* val)
	{
		return val;
	}

	template <typename Elem>
	const void* getOutPtr(const Array<Elem>& val)
	{
		return val.elem;
	}

	template <typename T>
	Hex<T> hex(T val)
	{
		return Hex<T>(val);
	}

	inline Array<HexByte> hexDump(const void* buf, const unsigned long size)
	{
		return Array<HexByte>(static_cast<const HexByte*>(buf), size);
	}

	template <typename T>
	Out<T> out(const T& val)
	{
		return Out<T>(val);
	}

	template <typename Elem>
	LogStream operator<<(LogStream os, Array<Elem> array)
	{
		os << '[';
		if (0 != array.size)
		{
			os << array.elem[0];
		}
		for (unsigned long i = 1; i < array.size; ++i)
		{
			os << ',' << array.elem[i];
		}
		return os << ']';
	}

	template <typename T>
	LogStream operator<<(LogStream os, Hex<T> hex)
	{
		return os << "0x" << std::hex << hex.val << std::dec;
	}

	inline LogStream operator<<(LogStream os, HexByte hexByte)
	{
		auto fill = os.getStream().fill('0');
		return os << std::setw(2) << std::hex << static_cast<DWORD>(hexByte.val) << std::dec << std::setfill(fill);
	}

	template <typename T>
	LogStream operator<<(LogStream os, Out<T> out)
	{
		if (Log::isLeaveLog())
		{
			os << out.val;
		}
		else
		{
			os.getStream() << '<' << getOutPtr(out.val) << '>';
		}
		return os;
	}

	class Log
	{
	public:
		Log();
		~Log();

		template <typename T>
		Log& operator<<(const T& t)
		{
			LogStream(s_logFile) << t;
			return *this;
		}

		static void initLogging(std::filesystem::path processPath);
		static bool isLeaveLog() { return s_isLeaveLog; }

	private:
		friend class LogFuncBase;

		ScopedCriticalSection m_lock;

		static thread_local DWORD s_indent;

		static bool s_isLeaveLog;
		static std::ofstream s_logFile;
	};

	template <auto funcPtr, int paramIndex, typename = decltype(funcPtr)>
	class LogParam;

	template <int paramIndex>
	class LogParam<nullptr, paramIndex, nullptr_t>
	{
	public:
		template <typename... Params>
		static void log(Log& log, Params&... params)
		{
			auto& param = std::get<paramIndex>(std::tie(params...));
			if constexpr (IsString<decltype(param)>::value)
			{
				if constexpr (!std::is_class_v<decltype(param)>)
				{
					if (reinterpret_cast<DWORD>(param) <= 0xFFFF)
					{
						log << param;
						return;
					}
				}
				log << '"' << param << '"';
			}
			else
			{
				log << param;
			}
		}
	};

	template <auto funcPtr, int paramIndex, typename Result, typename... Params>
	class LogParam<funcPtr, paramIndex, Result(WINAPI*)(Params...)>
	{
	public:
		static void log(Log& log, Params... params)
		{
			LogParam<nullptr, paramIndex>::log(log, params...);
		}
	};

	class LogFuncBase
	{
	public:
		template <typename T>
		T setResult(T result)
		{
			m_logResult = [=](Log& log) { log << std::hex << result << std::dec; };
			return result;
		}

	protected:
		template <typename... Params>
		LogFuncBase(const char* funcName, std::function<void(Log&)> logParams)
			: m_funcName(funcName)
			, m_logParams(logParams)
		{
			Log log;
			log << "> ";
			logCall(log);
			Log::s_indent += 2;
		}

		~LogFuncBase()
		{
			Log::s_isLeaveLog = true;
			Log::s_indent -= 2;
			Log log;
			log << "< ";
			logCall(log);

			if (m_logResult)
			{
				log << " = ";
				m_logResult(log);
			}
			Log::s_isLeaveLog = false;
		}

		template <typename Param>
		auto packParam(Param&& param)
		{
			if constexpr (std::is_lvalue_reference_v<Param>)
			{
				return std::cref(param);
			}
			else
			{
				return param;
			}
		}

		template <typename... Params>
		auto packParams(Params&&... params)
		{
			return std::make_tuple(packParam(std::forward<Params>(params))...);
		}

	private:
		void logCall(Log& log)
		{
			log << m_funcName << '(';
			m_logParams(log);
			log << ')';
		}

		const char* m_funcName;
		std::function<void(Log&)> m_logParams;
		std::function<void(Log&)> m_logResult;
	};

	template <auto funcPtr = nullptr>
	class LogFunc : public LogFuncBase
	{
	public:
		template <typename... Params>
		LogFunc(const char* funcName, Params&&... params)
			: LogFuncBase(funcName, [&, p = packParams(std::forward<Params>(params)...)](Log& log) { logParams(log, p); })
		{
		}

	private:
		template <int paramIndex>
		void logParams(Log&)
		{
		}

		template <int paramIndex, typename... Params>
		void logParams(Log& log, Params&... params)
		{
			if constexpr (paramIndex > 0)
			{
				log << ", ";
			}

			LogParam<funcPtr, paramIndex>::log(log, params...);

			if constexpr (paramIndex + 1 < sizeof...(Params))
			{
				logParams<paramIndex + 1>(log, params...);
			}
		}

		template <typename Pack>
		void logParams(Log& log, const Pack& pack)
		{
			std::apply([&](auto&... params) { logParams<0>(log, params...); }, pack);
		}
	};

	class LogStruct
	{
	public:
		LogStruct(std::ostream& os) : m_os(os), m_isFirst(true) { m_os << '{'; }
		~LogStruct() { m_os << '}'; }

		template <typename T>
		LogStruct& operator<<(const T& val)
		{
			if (m_isFirst)
			{
				m_isFirst = false;
			}
			else
			{
				m_os << ',';
			}
			m_os << val;
			return *this;
		}

		operator LogStream() const { return m_os; }
		operator std::ostream& () const { return m_os.getStream(); }

	private:
		LogStream m_os;
		bool m_isFirst;
	};
}
