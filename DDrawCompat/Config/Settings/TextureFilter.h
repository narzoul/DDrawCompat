#pragma once

#include <tuple>

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class TextureFilter : public MappedSetting<std::tuple<UINT, UINT, UINT>>
		{
		public:
			TextureFilter();

			UINT getFilter() const { return std::get<0>(m_value); }
			UINT getMipFilter() const { return std::get<1>(m_value); }
			UINT getMaxAnisotropy() const { return std::get<2>(m_value); }
		};
	}
}
