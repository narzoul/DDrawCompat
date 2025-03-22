#include <bitset>
#include <map>
#include <sstream>
#include <vector>

#include <Windows.h>
#include <avrt.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/Path.h>
#include <Common/ScopedCriticalSection.h>
#include <Common/Time.h>
#include <Config/Settings/CpuAffinity.h>
#include <Config/Settings/CpuAffinityRotation.h>
#include <Config/Settings/ThreadPriorityBoost.h>
#include <Dll/Dll.h>
#include <Win32/Thread.h>

struct THREAD_BASIC_INFORMATION
{
	NTSTATUS  ExitStatus;
	PVOID     TebBaseAddress;
	CLIENT_ID ClientId;
	KAFFINITY AffinityMask;
	KPRIORITY Priority;
	KPRIORITY BasePriority;
};

NTSTATUS NTAPI NtSetInformationThread(
	HANDLE ThreadHandle,
	THREADINFOCLASS ThreadInformationClass,
	PVOID ThreadInformation,
	ULONG ThreadInformationLength);

namespace
{
	struct ThreadInfo
	{
		DWORD id;
		HANDLE handle;
		const void* startAddress;
		bool useCpuAffinity;
	};

	Compat::CriticalSection g_cs;
	DWORD g_cpuAffinity = 0;
	std::map<BYTE, BYTE> g_nextProcessor;
	std::map<DWORD, ThreadInfo> g_threads;

	HANDLE g_exclusiveModeMutex = nullptr;
	thread_local bool g_skipWaitingForExclusiveModeMutex = false;

	decltype(&NtQueryInformationThread) g_ntQueryInformationThread = nullptr;
	decltype(&NtSetInformationThread) g_ntSetInformationThread = nullptr;

	bool useCpuAffinityRotation();
	std::string maskToString(ULONG_PTR mask);
	void registerThread(DWORD threadId, const char* msg);
	void setNextProcessorSet(DWORD fromSet, DWORD toSet);
	bool useCpuAffinityForThread(const void* startAddress);

	HANDLE WINAPI ddrawOpenMutexW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName)
	{
		LOG_FUNC("ddrawOpenMutexW", dwDesiredAccess, bInheritHandle, lpName);
		auto result = CALL_ORIG_FUNC(OpenMutexW)(dwDesiredAccess, bInheritHandle, lpName);
		if (SUCCEEDED(result) && lpName && 0 == lstrcmpW(lpName, L"Local\\__DDrawExclMode__"))
		{
			g_exclusiveModeMutex = result;
		}
		return LOG_RESULT(result);
	}

	DWORD WINAPI ddrawWaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds)
	{
		LOG_FUNC("ddrawWaitForSingleObject", hHandle, dwMilliseconds);
		if (hHandle && hHandle == g_exclusiveModeMutex && g_skipWaitingForExclusiveModeMutex)
		{
			return WAIT_OBJECT_0;
		}
		return LOG_RESULT(WaitForSingleObject(hHandle, dwMilliseconds));
	}

	std::map<BYTE, std::vector<DWORD>> getProcessorSets()
	{
		std::map<BYTE, std::vector<DWORD>> result;
		DWORD primaryProcessorGroup = 0;
		auto getThreadIdealProcessorEx = reinterpret_cast<decltype(&GetThreadIdealProcessorEx)>(
			Compat::getProcAddress(GetModuleHandle("kernel32"), "GetThreadIdealProcessorEx"));
		if (getThreadIdealProcessorEx)
		{
			PROCESSOR_NUMBER pn = {};
			getThreadIdealProcessorEx(GetCurrentThread(), &pn);
			primaryProcessorGroup = pn.Group;
			LOG_INFO << "Primary CPU group number: " << primaryProcessorGroup;
		}

		auto getLogicalProcessorInformationEx = reinterpret_cast<decltype(&GetLogicalProcessorInformationEx)>(
			Compat::getProcAddress(GetModuleHandle("kernel32"), "GetLogicalProcessorInformationEx"));
		if (getLogicalProcessorInformationEx)
		{
			DWORD size = 0;
			std::vector<BYTE> buffer;
			getLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &size);
			buffer.resize(size);
			getLogicalProcessorInformationEx(RelationProcessorCore,
				reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data()), &size);

			DWORD offset = 0;
			while (offset < size)
			{
				auto& pi = *reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
				for (DWORD i = 0; i < pi.Processor.GroupCount; ++i)
				{
					if (primaryProcessorGroup == pi.Processor.GroupMask[i].Group)
					{
						result[pi.Processor.EfficiencyClass].push_back(pi.Processor.GroupMask[i].Mask);
					}
				}
				offset += pi.Size;
			}
		}
		else
		{
			DWORD size = 0;
			GetLogicalProcessorInformation(nullptr, &size);
			std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> processorInfo;
			processorInfo.resize(size / sizeof(processorInfo[0]));
			GetLogicalProcessorInformation(processorInfo.data(), &size);

			for (auto& pi : processorInfo)
			{
				if (RelationProcessorCore == pi.Relationship)
				{
					result[0].push_back(pi.ProcessorMask);
				}
			}
		}

		if (result.empty())
		{
			LOG_INFO << "ERROR: Failed to detect CPU topology!";
			return result;
		}

		for (auto it = result.rbegin(); it != result.rend(); ++it)
		{
			LOG_INFO << "Physical to logical CPU core mapping for efficiency class " << static_cast<DWORD>(it->first) << ':';
			DWORD core = 0;
			for (auto& set : it->second)
			{
				++core;
				LOG_INFO << "  Physical core #" << core << ": " << maskToString(set);
			}
		}
		return result;
	}

	void initNextProcessorMap(const std::map<BYTE, std::vector<DWORD>>& processorSets)
	{
		for (auto& ps : processorSets)
		{
			for (BYTE fromIndex = 0; fromIndex < ps.second.size(); ++fromIndex)
			{
				auto bitCount = std::bitset<32>(ps.second[fromIndex]).count();
				bool found = false;
				for (BYTE toIndex = fromIndex + 1; toIndex < ps.second.size() && !found; ++toIndex)
				{
					if (bitCount == std::bitset<32>(ps.second[toIndex]).count())
					{
						setNextProcessorSet(ps.second[fromIndex], ps.second[toIndex]);
						found = true;
					}
				}
				for (BYTE toIndex = 0; toIndex < fromIndex && !found; ++toIndex)
				{
					if (bitCount == std::bitset<32>(ps.second[toIndex]).count())
					{
						setNextProcessorSet(ps.second[fromIndex], ps.second[toIndex]);
						found = true;
					}
				}
				if (!found)
				{
					setNextProcessorSet(ps.second[fromIndex], ps.second[fromIndex]);
				}
			}
		}
	}

	void logThreadInfo(ThreadInfo threadInfo, const char* msg)
	{
		if (Config::logLevel.get() < Config::Settings::LogLevel::DEBUG)
		{
			return;
		}

		Compat::Log log(Config::Settings::LogLevel::DEBUG);
		log << msg << ": ID: " << std::hex << std::setw(4) << std::setfill('0') << threadInfo.id << std::dec
			<< ", use CPU affinity: " << threadInfo.useCpuAffinity;
		if (threadInfo.startAddress)
		{
			log << " (" << Compat::funcPtrToStr(threadInfo.startAddress) << ')';
		}
	}

	std::string maskToString(ULONG_PTR mask)
	{
		std::ostringstream oss;
		for (BYTE i = 0; i < 32; ++i)
		{
			if (mask & (1U << i))
			{
				oss << i + 1 << ", ";
			}
		}
		return oss.str().empty() ? "null" : oss.str().substr(0, oss.str().length() - 2);
	}

	void registerExistingThreads()
	{
		LOG_FUNC("registerExistingThreads");
		const DWORD pid = GetCurrentProcessId();
		const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		if (INVALID_HANDLE_VALUE == snapshot)
		{
			LOG_INFO << "ERROR: CreateToolhelp32Snapshot failed: " << GetLastError();
			return;
		}

		THREADENTRY32 te = {};
		te.dwSize = sizeof(te);
		if (!Thread32First(snapshot, &te))
		{
			CloseHandle(snapshot);
			LOG_INFO << "ERROR: Thread32First failed: " << GetLastError();
			return;
		}

		do
		{
			if (pid == te.th32OwnerProcessID)
			{
				registerThread(te.th32ThreadID, "Thread running");
			}
		} while (Thread32Next(snapshot, &te));

		CloseHandle(snapshot);
	}

	void registerThread(DWORD threadId, const char* msg)
	{
		Compat::ScopedCriticalSection lock(g_cs);
		if (g_threads.find(threadId) != g_threads.end())
		{
			return;
		}

		ThreadInfo threadInfo = {};
		threadInfo.id = threadId;
		threadInfo.handle = OpenThread(THREAD_QUERY_INFORMATION | THREAD_SET_INFORMATION, FALSE, threadId);
		if (!threadInfo.handle)
		{
			LOG_ONCE("ERROR: OpenThread failed: " << GetLastError());
			return;
		}

		if (g_ntQueryInformationThread)
		{
			const auto ThreadQuerySetWin32StartAddress = static_cast<THREADINFOCLASS>(9);
			g_ntQueryInformationThread(threadInfo.handle, ThreadQuerySetWin32StartAddress,
				&threadInfo.startAddress, sizeof(threadInfo.startAddress), nullptr);
		}

		threadInfo.useCpuAffinity = useCpuAffinityForThread(threadInfo.startAddress);
		g_threads[threadId] = threadInfo;
		logThreadInfo(threadInfo, msg);

		if (threadInfo.useCpuAffinity)
		{
			SetThreadAffinityMask(threadInfo.handle, g_cpuAffinity);
		}
	}

	DWORD rotateMask(DWORD mask)
	{
		DWORD result = 0;
		for (BYTE i = 0; i < 32; ++i)
		{
			if (mask & (1U << i))
			{
				auto it = g_nextProcessor.find(i);
				result |= 1U << (it != g_nextProcessor.end() ? it->second : i);
			}
		}
		return result;
	}

	void setNextProcessorSet(DWORD fromSet, DWORD toSet)
	{
		BYTE from = 0;
		BYTE to = 0;
		while (0 != fromSet)
		{
			while (!(fromSet & (1U << from)))
			{
				++from;
			}
			while (!(toSet & (1U << to)))
			{
				++to;
			}
			g_nextProcessor[from] = to;
			fromSet &= ~(1U << from);
			++from;
			++to;
		}
	}

	void setProcessAffinity()
	{
		if (0 == g_cpuAffinity)
		{
			return;
		}

		Compat::ScopedCriticalSection lock(g_cs);
		for (const auto& t : g_threads)
		{
			if (t.second.useCpuAffinity)
			{
				auto prev = SetThreadAffinityMask(t.second.handle, g_cpuAffinity);
				LOG_DEBUG << "Thread affinity changed from " << Compat::hex(prev) << " to " << Compat::hex(g_cpuAffinity)
					<< " for thread ID " << Compat::hex(t.second.id);
			}
		}
	}

	unsigned WINAPI threadManagerProc(LPVOID /*lpParameter*/)
	{
		DWORD taskIndex = 0;
		HANDLE task = AvSetMmThreadCharacteristicsA("Audio", &taskIndex);
		if (!task)
		{
			LOG_INFO << "ERROR: AvSetMmThreadCharacteristicsA failed: " << GetLastError();
		}
		else if (!AvSetMmThreadPriority(task, AVRT_PRIORITY_CRITICAL))
		{
			LOG_INFO << "ERROR: AvSetMmThreadPriority failed: " << GetLastError();
		}

		const bool rotate = useCpuAffinityRotation();

		while (true)
		{
			Sleep(100);

			{
				Compat::ScopedCriticalSection lock(g_cs);
				for (const auto& t : g_threads)
				{
					if (WAIT_OBJECT_0 == WaitForSingleObject(t.second.handle, 0))
					{
						logThreadInfo(t.second, "Thread terminated");
						CloseHandle(t.second.handle);
						g_threads.erase(t.first);
					}
				}

				if (rotate)
				{
					g_cpuAffinity = rotateMask(g_cpuAffinity);
					setProcessAffinity();
				}

				if (!g_ntQueryInformationThread || !g_ntSetInformationThread)
				{
					continue;
				}

				for (const auto& t : g_threads)
				{
					const auto ThreadBasicInformation = static_cast<THREADINFOCLASS>(0);
					THREAD_BASIC_INFORMATION tbi = {};
					g_ntQueryInformationThread(t.second.handle, ThreadBasicInformation, &tbi, sizeof(tbi), nullptr);

					if (tbi.Priority > THREAD_PRIORITY_TIME_CRITICAL)
					{
						const auto ThreadPriority = static_cast<THREADINFOCLASS>(2);
						ULONG prio = THREAD_PRIORITY_TIME_CRITICAL;
						g_ntSetInformationThread(t.second.handle, ThreadPriority, &prio, sizeof(prio));
					}
				}
			}

			if (rotate)
			{
				SetThreadAffinityMask(GetCurrentThread(), rotateMask(g_cpuAffinity));
			}
		}
	}

	bool useCpuAffinityForThread(const void* startAddress)
	{
		if (0 == g_cpuAffinity)
		{
			return false;
		}

		if (!startAddress)
		{
			return true;
		}

		HMODULE mod = Compat::getModuleHandleFromAddress(startAddress);
		if (!mod)
		{
			return true;
		}

		if (mod == Dll::g_currentModule)
		{
			return false;
		}

		auto path = Compat::getModulePath(mod);
		auto filename = path.filename();
		return !Compat::isEqual("dinput.dll", filename) &&
			!Compat::isEqual("dinput8.dll", filename) &&
			!Compat::isEqual("dsound.dll", filename) &&
			!Compat::isPrefix(Compat::getSystemPath() / "DriverStore" / "", path);
	}

	bool useCpuAffinityRotation()
	{
		Compat::ScopedCriticalSection lock(g_cs);
		return Config::cpuAffinityRotation.get() && rotateMask(g_cpuAffinity) != g_cpuAffinity;
	}

	HANDLE WINAPI createThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize,
		LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId)
	{
		LOG_FUNC("CreateThread", lpThreadAttributes, dwStackSize, lpStartAddress, lpParameter, dwCreationFlags, lpThreadId);
		if (0 == g_cpuAffinity)
		{
			return LOG_RESULT(CALL_ORIG_FUNC(CreateThread)(
				lpThreadAttributes, dwStackSize, lpStartAddress, lpParameter, dwCreationFlags, lpThreadId));
		}

		const auto thread = CALL_ORIG_FUNC(CreateThread)(
			lpThreadAttributes, dwStackSize, lpStartAddress, lpParameter, dwCreationFlags | CREATE_SUSPENDED, lpThreadId);
		if (thread)
		{
			registerThread(GetThreadId(thread), "Thread started");
			if (!(dwCreationFlags & CREATE_SUSPENDED))
			{
				ResumeThread(thread);
			}
		}
		return LOG_RESULT(thread);
	}

	BOOL WINAPI setProcessAffinityMask(HANDLE hProcess, DWORD_PTR dwProcessAffinityMask)
	{
		LOG_FUNC("SetProcessAffinityMask", hProcess, Compat::hex(dwProcessAffinityMask));
		if (0 != Config::cpuAffinity.get())
		{
			return LOG_RESULT(TRUE);
		}
		return LOG_RESULT(CALL_ORIG_FUNC(SetProcessAffinityMask)(hProcess, dwProcessAffinityMask));
	}

	BOOL WINAPI setProcessPriorityBoost(HANDLE hProcess, BOOL bDisablePriorityBoost)
	{
		LOG_FUNC("SetProcessPriorityBoost", hProcess, bDisablePriorityBoost);
		if (Config::Settings::ThreadPriorityBoost::APP != Config::threadPriorityBoost.get())
		{
			return LOG_RESULT(TRUE);
		}
		return LOG_RESULT(CALL_ORIG_FUNC(SetProcessPriorityBoost)(hProcess, bDisablePriorityBoost));
	}

	BOOL WINAPI setThreadPriorityBoost(HANDLE hThread, BOOL bDisablePriorityBoost)
	{
		LOG_FUNC("SetThreadPriorityBoost", hThread, bDisablePriorityBoost);
		if (Config::Settings::ThreadPriorityBoost::APP != Config::threadPriorityBoost.get())
		{
			return LOG_RESULT(TRUE);
		}
		return LOG_RESULT(CALL_ORIG_FUNC(SetThreadPriorityBoost)(hThread, bDisablePriorityBoost));
	}
}

namespace Win32
{
	namespace Thread
	{
		void applyConfig()
		{
			initNextProcessorMap(getProcessorSets());

			DWORD processMask = 0;
			DWORD systemMask = 0;
			GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask);
			if (0 == systemMask)
			{
				LOG_INFO << "ERROR: Failed to determine system CPU affinity mask";
			}

			g_cpuAffinity = Config::cpuAffinity.get();
			if (0 != g_cpuAffinity)
			{
				for (BYTE i = 0; i < 32; ++i)
				{
					if (g_nextProcessor.find(i) == g_nextProcessor.end())
					{
						g_cpuAffinity &= ~(1U << i);
					}
				}

				if (0 == g_cpuAffinity)
				{
					LOG_INFO << "Warning: CPU affinity setting doesn't match any existing logical cores, using default";
					g_cpuAffinity = 1;
				}
			}
			else
			{
				g_cpuAffinity = systemMask;
			}

			switch (Config::threadPriorityBoost.get())
			{
			case Config::Settings::ThreadPriorityBoost::OFF:
				CALL_ORIG_FUNC(SetProcessPriorityBoost)(GetCurrentProcess(), TRUE);
				break;

			case Config::Settings::ThreadPriorityBoost::ON:
				CALL_ORIG_FUNC(SetProcessPriorityBoost)(GetCurrentProcess(), FALSE);
				break;

			case Config::Settings::ThreadPriorityBoost::MAIN:
				CALL_ORIG_FUNC(SetProcessPriorityBoost)(GetCurrentProcess(), TRUE);
				CALL_ORIG_FUNC(SetThreadPriorityBoost)(GetCurrentThread(), FALSE);
				break;
			}

			LOG_INFO << "Process priority class: " << GetPriorityClass(GetCurrentProcess());
			LOG_INFO << "Current CPU affinity: " << maskToString(processMask);
			if (0 != g_cpuAffinity)
			{
				LOG_INFO << "Applying configured CPU affinity: " << maskToString(g_cpuAffinity);
				CALL_ORIG_FUNC(SetProcessAffinityMask)(GetCurrentProcess(), systemMask);
			}

			LOG_INFO << "CPU affinity rotation is " << (useCpuAffinityRotation() ? "enabled" : "disabled");

			registerExistingThreads();
			setProcessAffinity();
			Dll::createThread(&threadManagerProc, nullptr, THREAD_PRIORITY_TIME_CRITICAL);
		}

		void dllThreadAttach()
		{
			if (0 != g_cpuAffinity)
			{
				registerThread(GetCurrentThreadId(), "Thread started");
			}
		}

		void dllThreadDetach()
		{
			if (0 == g_cpuAffinity)
			{
				return;
			}

			Compat::ScopedCriticalSection lock(g_cs);
			auto it = g_threads.find(GetCurrentThreadId());
			if (it != g_threads.end())
			{
				logThreadInfo(it->second, "Thread stopped");
				CloseHandle(it->second.handle);
				g_threads.erase(it);
			}
		}

		void installHooks()
		{
			HOOK_FUNCTION(kernel32, CreateThread, createThread);
			HOOK_FUNCTION(kernel32, SetProcessAffinityMask, setProcessAffinityMask);
			HOOK_FUNCTION(kernel32, SetProcessPriorityBoost, setProcessPriorityBoost);
			HOOK_FUNCTION(kernel32, SetThreadPriorityBoost, setThreadPriorityBoost);

			Compat::hookIatFunction(Dll::g_origDDrawModule, "OpenMutexW", ddrawOpenMutexW);
			Compat::hookIatFunction(Dll::g_origDDrawModule, "WaitForSingleObject", ddrawWaitForSingleObject);

			g_ntQueryInformationThread = GET_PROC_ADDRESS(ntdll, NtQueryInformationThread);
			g_ntSetInformationThread = GET_PROC_ADDRESS(ntdll, NtSetInformationThread);
		}

		void skipWaitingForExclusiveModeMutex(bool skip)
		{
			g_skipWaitingForExclusiveModeMutex = skip;
		}
	}
}
