#pragma once

#include <vector>

#include <Config/MappedSetting.h>

namespace Config
{
	class EnumSetting : public MappedSetting<unsigned>
	{
	protected:
		EnumSetting(const std::string& name, unsigned default, const std::vector<std::string>& enumNames);
	};
}
