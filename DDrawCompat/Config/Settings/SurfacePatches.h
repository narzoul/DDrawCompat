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

			unsigned getTop() const { return m_top; }
			unsigned getBottom() const { return m_bottom; }

		private:
			virtual std::string addValue(const std::string& value) override;
			virtual void clear() override;

			unsigned m_top;
			unsigned m_bottom;
		};
	}

	extern Settings::SurfacePatches surfacePatches;
}
