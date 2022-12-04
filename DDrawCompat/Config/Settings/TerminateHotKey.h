#pragma once

#include <Config/HotKeySetting.h>

namespace Config
{
	namespace Settings
	{
		class TerminateHotKey : public HotKeySetting
		{
		public:
			TerminateHotKey() : HotKeySetting("TerminateHotKey", "ctrl+alt+end") {}
		};
	}
}
