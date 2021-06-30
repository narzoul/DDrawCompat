#include <algorithm>

#include <Config/Parser.h>
#include <Config/EnumSetting.h>

namespace
{
	std::map<std::string, unsigned> createMapping(const std::vector<std::string>& enumNames)
	{
		std::map<std::string, unsigned> mapping;
		unsigned i = 0;
		for (const auto& name : enumNames)
		{

			mapping[name] = i;
			++i;
		}
		return mapping;
	}
}

namespace Config
{
	EnumSetting::EnumSetting(const std::string& name, const std::string& default, const std::vector<std::string>& enumNames)
		: MappedSetting(name, default, createMapping(enumNames))
	{
	}
}
