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

			virtual std::string getValueStr() const override;

			unsigned getTop() const { return m_top; }
			unsigned getBottom() const { return m_bottom; }

		private:
			void setValues(const std::vector<std::string>& values) override;

			unsigned m_top;
			unsigned m_bottom;
		};
	}

	extern Settings::SurfacePatches surfacePatches;
}
