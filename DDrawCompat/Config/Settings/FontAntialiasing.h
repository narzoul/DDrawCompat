#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class FontAntialiasing : public MappedSetting<UINT>
		{
		public:
			static const UINT APP = 0;
			static const UINT OFF = 1;
			static const UINT ON = 2;

			FontAntialiasing()
				: MappedSetting("FontAntialiasing", "app", { {"app", APP}, {"off", OFF}, {"on", ON} })
			{
			}
		};
	}

	extern Settings::FontAntialiasing fontAntialiasing;
}
