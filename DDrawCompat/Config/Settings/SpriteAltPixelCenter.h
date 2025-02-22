#pragma once

#include <Config/MappedSetting.h>
#include <Config/Settings/AlternatePixelCenter.h>

namespace Config
{
	namespace Settings
	{
		class SpriteAltPixelCenter : public MappedSetting<float>
		{
		public:
			SpriteAltPixelCenter()
				: MappedSetting("SpriteAltPixelCenter", "apc",
					{ {"off", 0.0f}, {"on", -0.5f}, {"custom", INFINITY}, {"apc", -INFINITY} })
			{
			}

			float get() const
			{
				if (-INFINITY == m_value)
				{
					return alternatePixelCenter.get();
				}
				return m_value == INFINITY ? m_param / 100.0f : m_value;
			}

			virtual ParamInfo getParamInfo() const override
			{
				if (INFINITY == m_value)
				{
					return { "Offset", -100, 100, -50 };
				}
				return {};
			}
		};
	}

	extern Settings::SpriteAltPixelCenter spriteAltPixelCenter;
}
