#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		struct VersionInfo
		{
			DWORD version;
			DWORD build;
			DWORD platform;

			bool operator==(const VersionInfo& other) const;
		};

		class WinVersionLie : public MappedSetting<VersionInfo>
		{
		public:
			WinVersionLie();

			virtual ParamInfo getParamInfo() const override;
		};
	}

	extern Settings::WinVersionLie winVersionLie;
}
