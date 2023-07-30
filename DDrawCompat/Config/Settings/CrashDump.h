#pragma once

#include <DbgHelp.h>

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class CrashDump : public MappedSetting<UINT>
		{
		public:
			static const UINT OFF = 0;

			static const UINT MINI =
				MiniDumpWithDataSegs |
				MiniDumpWithUnloadedModules |
				MiniDumpWithProcessThreadData;

			static const UINT FULL =
				MiniDumpWithFullMemory |
				MiniDumpWithHandleData |
				MiniDumpWithUnloadedModules |
				MiniDumpWithFullMemoryInfo |
				MiniDumpWithThreadInfo |
				MiniDumpIgnoreInaccessibleMemory;

			CrashDump() : MappedSetting("CrashDump", "off", { {"off", OFF}, {"mini", MINI}, {"full", FULL} })
			{
			}
		};
	}

	extern Settings::CrashDump crashDump;
}
