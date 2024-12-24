#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class MousePollingRate : public MappedSetting<UINT>
		{
		public:
			static const UINT NATIVE = 0;

			MousePollingRate()
				: MappedSetting("MousePollingRate", "native", {
					{"native", NATIVE},
					{"125", 125},
					{"250", 250},
					{"500", 500},
					{"1000", 1000}
					})
			{
			}
		};
	}

	extern Settings::MousePollingRate mousePollingRate;
}
