#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class DesktopColorDepth : public MappedSetting<UINT>
		{
		public:
			static const UINT INITIAL = 0;

			DesktopColorDepth()
				: MappedSetting("DesktopColorDepth", "initial", { {"initial", INITIAL}, {"8", 8}, {"16", 16}, {"32", 32} })
			{
			}
		};
	}
}
