#pragma once

#include <Config/FormatListSetting.h>

namespace Config
{
	namespace Settings
	{
		class SupportedTextureFormats : public FormatListSetting
		{
		public:
			SupportedTextureFormats();
		};
	}

	extern Settings::SupportedTextureFormats supportedTextureFormats;
}
