#pragma once

#include <set>

#include <Windows.h>

#include <Config/ListSetting.h>

namespace Config
{
	namespace Settings
	{
		class SupportedTextureFormats : public ListSetting
		{
		public:
			SupportedTextureFormats();

			virtual std::string getValueStr() const override;

			bool isSupported(UINT format) const;

		private:
			void setValues(const std::vector<std::string>& values) override;

			std::set<UINT> m_formats;
		};
	}

	extern Settings::SupportedTextureFormats supportedTextureFormats;
}
