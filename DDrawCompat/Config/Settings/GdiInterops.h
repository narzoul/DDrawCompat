#pragma once

#include <Config/ListSetting.h>

namespace Config
{
	namespace Settings
	{
		class GdiInterops : public ListSetting
		{
		public:
			struct Interops
			{
				bool colors;
				bool desktop;
				bool dialogs;
				bool themes;
				bool windows;
			};

			GdiInterops();

			bool anyRedirects() const { return m_interops.desktop || m_interops.dialogs || m_interops.windows; }
			const Interops& get() const { return m_interops; }

		private:
			virtual std::string addValue(const std::string& value) override;
			virtual void clear() override;

			Interops m_interops;
		};
	}

	extern Settings::GdiInterops gdiInterops;
}
