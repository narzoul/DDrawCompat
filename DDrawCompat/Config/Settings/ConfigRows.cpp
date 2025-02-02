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

		std::string ConfigRows::addValue(const std::string& value)
		{
			if ("all" == value)
			{
				appendAll(m_settings);
				return value;
			}

			if ("allro" == value)
			{
				appendAllRo(m_settings);
				return value;
			}

			if ("allrw" == value)
			{
				appendAllRw(m_settings);
				return value;
			}

			auto setting = getSetting(value);
			if (!setting)
			{
				throw ParsingError("invalid value: '" + value + "'");
			}

			const bool overwrite = true;
			append(m_settings, setting, overwrite);
			return setting->getName();
		}

		void ConfigRows::clear()
		{
			m_settings.clear();
		}
	}
}
