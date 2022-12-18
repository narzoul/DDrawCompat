#pragma once

#include <Config/FormatListSetting.h>

namespace Config
{
	namespace Settings
	{
		class SupportedDepthFormats : public FormatListSetting
		{
		public:
			SupportedDepthFormats();
		};
	}

	extern Settings::SupportedDepthFormats supportedDepthFormats;
}
