#pragma once

#include <vector>

#include <Config/MappedSetting.h>

namespace Config
{
	class EnumSetting : public MappedSetting<unsigned>
	{
	protected:
		EnumSetting(const std::string& name, const std::string& defaultValue,
			const std::vector<std::string>& enumNames);
	};
}
