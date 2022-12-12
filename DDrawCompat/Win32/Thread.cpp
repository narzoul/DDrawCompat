#include <bitset>
#include <map>
#include <sstream>
#include <vector>

#include <Windows.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/Time.h>
#include <Config/Config.h>
#include <Dll/Dll.h>
#include <Win32/Thread.h>

namespace
{
	DWORD g_cpuAffinity = 0;
	bool g_cpuAffinityRotationEnabled = false;
	std::map<BYTE, BYTE> g_nextProcessor;

	std::string maskToString(ULONG_PTR mask);
	void setNextProcessorSet(DWORD fromSet, DWORD toSet);

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
		return oss.str().substr(0, max(0, oss.str().length() - 2));
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

			DWORD processMask = 0;
			DWORD systemMask = 0;
			GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask);
			LOG_INFO << "Current CPU affinity: " << maskToString(processMask);

			if (0 == g_cpuAffinity && 0 != processMask && processMask != systemMask)
			{
				g_cpuAffinity = processMask;
			}

			if (0 != g_cpuAffinity)
			{
				LOG_INFO << "Applying configured CPU affinity: " << maskToString(g_cpuAffinity);
				if (!CALL_ORIG_FUNC(SetProcessAffinityMask)(GetCurrentProcess(), g_cpuAffinity))
				{
					LOG_INFO << "ERROR: Failed to set CPU affinity";
					g_cpuAffinity = 0;
				}
			}

			g_cpuAffinityRotationEnabled = Config::cpuAffinityRotation.get() && rotateMask(g_cpuAffinity) != g_cpuAffinity;
			LOG_INFO << "CPU affinity rotation is " << (g_cpuAffinityRotationEnabled ? "enabled" : "disabled");

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
		}

		void installHooks()
		{
			HOOK_FUNCTION(kernel32, SetProcessAffinityMask, setProcessAffinityMask);
			HOOK_FUNCTION(kernel32, SetProcessPriorityBoost, setProcessPriorityBoost);
			HOOK_FUNCTION(kernel32, SetThreadPriorityBoost, setThreadPriorityBoost);
		}

		void rotateCpuAffinity()
		{
			if (!g_cpuAffinityRotationEnabled)
			{
				return;
			}

			static auto g_qpcLastRotation = Time::queryPerformanceCounter();
			auto qpcNow = Time::queryPerformanceCounter();
			if (qpcNow - g_qpcLastRotation < Time::g_qpcFrequency / 10)
			{
				return;
			}
			g_qpcLastRotation = qpcNow;

			g_cpuAffinity = rotateMask(g_cpuAffinity);
			if (!CALL_ORIG_FUNC(SetProcessAffinityMask)(GetCurrentProcess(), g_cpuAffinity))
			{
				LOG_ONCE("ERROR: Failed to set rotated CPU affinity: " << maskToString(g_cpuAffinity));
			}
		}
	}
}
