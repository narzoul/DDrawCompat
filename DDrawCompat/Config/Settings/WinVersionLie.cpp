#include <Config/Settings/WinVersionLie.h>

namespace Config
{
	namespace Settings
	{
		bool VersionInfo::operator==(const VersionInfo& other) const
		{
			return version == other.version &&
				build == other.build &&
				platform == other.platform;
		}

		WinVersionLie::WinVersionLie()
			: MappedSetting("WinVersionLie", "off", {
				{"off", {}},
				{"95", {0xC3B60004, 0x3B6, 1}},
				{"nt4", {0x5650004, 0x565, 2}},
				{"98", {0xC0000A04, 0x40A08AE, 1}},
				{"2000", {0x8930005, 0x893, 2}},
				{"xp", {0xA280105, 0xA28, 2}}
				})
		{
		}

		Setting::ParamInfo WinVersionLie::getParamInfo() const
		{
			if (0 != m_value.version)
			{
				return { "SP", 0, 5, 0 };
			}
			return {};
		}
	}
}
