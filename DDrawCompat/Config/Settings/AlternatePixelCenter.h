#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class AlternatePixelCenter : public MappedSetting<float>
		{
		public:
			AlternatePixelCenter()
				: MappedSetting("AlternatePixelCenter", "off", { {"off", 0.0f}, {"on", -0.5f}, {"custom", INFINITY} })
			{
			}

			float get() const
			{
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

	extern Settings::AlternatePixelCenter alternatePixelCenter;
}
