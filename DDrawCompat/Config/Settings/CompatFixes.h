#pragma once

#include <Config/ListSetting.h>

namespace Config
{
	namespace Settings
	{
		class CompatFixes : public ListSetting
		{
		public:
			struct Fixes
			{
				bool nodepthblt;
				bool nodepthlock;
				bool nodwmflush;
				bool nohalftone;
				bool nosyslock;
				bool nowindowborders;
				bool unalignedsurfaces;
			};

			CompatFixes();

			const Fixes& get() const { return m_fixes; }

		private:

			virtual std::string addValue(const std::string& value) override;
			virtual void clear() override;

			Fixes m_fixes;
		};
	}

	extern Settings::CompatFixes compatFixes;
}
