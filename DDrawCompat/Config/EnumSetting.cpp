#include <algorithm>

#include <Config/Parser.h>
#include <Config/EnumSetting.h>

namespace
{
	std::vector<std::pair<std::string, unsigned>> createMapping(const std::vector<std::string>& enumNames)
	{
		std::vector<std::pair<std::string, unsigned>> mapping;
		unsigned i = 0;
		for (const auto& name : enumNames)
		{
			mapping.push_back({ name, i });
			++i;
		}
		return mapping;
	}
}

namespace Config
{
	EnumSetting::EnumSetting(const std::string& name, const std::string& defaultValue,
		const std::vector<std::string>& enumNames)
		: MappedSetting(name, defaultValue, createMapping(enumNames))
	{
	}
}
