#include <algorithm>

#include <Config/Parser.h>
#include <Config/Settings/ConfigRows.h>
#include <Overlay/ConfigWindow.h>

namespace
{
	template <typename T>
	void append(std::vector<T>& values, const T& value, bool overwrite = false)
	{
		auto it = std::find(values.begin(), values.end(), value);
		if (it != values.end())
		{
			if (!overwrite)
			{
				return;
			}
			values.erase(it);
		}
		values.push_back(value);
	}

	void appendAll(std::vector<Config::Setting*>& settings)
	{
		const auto& allSettings = Config::getAllSettings();
		for (const auto& setting : allSettings)
		{
			append(settings, &setting.second);
		}
	}

	void appendAllRo(std::vector<Config::Setting*>& settings)
	{
		const auto& allSettings = Config::getAllSettings();
		const auto& rwSettingNames = Overlay::ConfigWindow::getRwSettingNames();
		for (const auto& setting : allSettings)
		{
			if (rwSettingNames.find(setting.first) == rwSettingNames.end())
			{
				append(settings, &setting.second);
			}
		}
	}

	void appendAllRw(std::vector<Config::Setting*>& settings)
	{
		const auto& rwSettingNames = Overlay::ConfigWindow::getRwSettingNames();
		for (const auto& settingName : rwSettingNames)
		{
			append(settings, Config::getSetting(settingName));
		}
	}
}

namespace Config
{
	namespace Settings
	{
		ConfigRows::ConfigRows()
			: ListSetting("ConfigRows", "allrw, allro")
		{
		}

		std::string ConfigRows::getValueStr() const
		{
			return m_valueStr;
		}

		void ConfigRows::setValues(const std::vector<std::string>& values)
		{
			std::set<std::string> groups;
			std::vector<Setting*> settings;
			std::vector<std::string> valueStr;

			for (auto value : values)
			{
				if ("all" == value || "allro" == value || "allrw" == value)
				{
					if (groups.find(value) != groups.end())
					{
						continue;
					}

					if ("all" == value)
					{
						appendAll(settings);
					}
					else if ("allro" == value)
					{
						appendAllRo(settings);
					}
					else if ("allrw" == value)
					{
						appendAllRw(settings);
					}

					valueStr.push_back(value);
					continue;
				}

				auto setting = getSetting(value);
				if (!setting)
				{
					throw ParsingError("invalid value: '" + value + "'");
				}

				const bool overwrite = true;
				append(settings, setting, overwrite);
				append(valueStr, setting->getName(), overwrite);
			}

			m_settings = settings;

			m_valueStr.clear();
			for (const auto& v : valueStr)
			{
				m_valueStr += ", " + v;
			}
			m_valueStr = m_valueStr.substr(2);
		}
	}
}
