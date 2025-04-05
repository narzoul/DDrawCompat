#pragma once

#include <Config/ListSetting.h>

namespace Config
{
	namespace Settings
	{
		class SurfacePatches : public ListSetting
		{
		public:
			SurfacePatches();

			unsigned getExtraRows(unsigned height);
			unsigned getTopRows(unsigned height);

		private:
			virtual std::string addValue(const std::string& value) override;
			virtual void clear() override;

			int m_top;
			int m_bottom;
		};
	}

	extern Settings::SurfacePatches surfacePatches;
}
