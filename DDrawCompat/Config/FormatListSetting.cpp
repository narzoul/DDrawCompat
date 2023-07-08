#include <algorithm>
#include <sstream>

#include <Config/FormatListSetting.h>
#include <Config/Parser.h>
#include <D3dDdi/Log/CommonLog.h>

namespace
{
	std::string getFormatName(D3DDDIFORMAT format)
	{
		if (format > 0xFF)
		{
			auto cc = reinterpret_cast<const char*>(&format);
			return Config::Parser::toupper(std::string(cc, cc + 4));
		}

		std::ostringstream oss;
		oss << format;
		return oss.str().substr(10);
	}
}

namespace Config
{
	FormatListSetting::FormatListSetting(const std::string& name, const std::string& default,
		const std::set<D3DDDIFORMAT>& allowedFormats,
		const std::map<std::string, std::set<D3DDDIFORMAT>>& allowedGroups,
		bool allowFourCCs)
		: ListSetting(name, default)
		, m_allowedFormats(allowedFormats)
		, m_allowedGroups(allowedGroups)
		, m_allowFourCCs(allowFourCCs)
		, m_valueStr("all")
	{
	}

	std::string FormatListSetting::getValueStr() const
	{
		if (m_formats.empty())
		{
			return "all";
		}

		std::string result;
		for (auto format : m_formats)
		{
			result += ", " + getFormatName(format);
		}
		return result.substr(2);
	}

	bool FormatListSetting::isSupported(D3DDDIFORMAT format) const
	{
		if (m_formats.empty())
		{
			return true;
		}
		return std::find(m_formats.begin(), m_formats.end(), format) != m_formats.end();
	}

	void FormatListSetting::setValues(const std::vector<std::string>& values)
	{
		if (values.empty())
		{
			throw ParsingError("empty list is not allowed");
		}

		if (std::find(values.begin(), values.end(), "all") != values.end())
		{
			m_formats.clear();
			m_valueStr = "all";
			return;
		}

		std::set<D3DDDIFORMAT> formats;
		std::set<std::string> groups;

		for (const auto& formatName : values)
		{
			auto group = m_allowedGroups.find(formatName);
			if (group != m_allowedGroups.end())
			{
				formats.insert(group->second.begin(), group->second.end());
				groups.insert(group->first);
			}
		}

		std::set<std::string> valueSet(groups);

		for (auto formatName : values)
		{
			if (groups.find(formatName) != groups.end())
			{
				continue;
			}

			formatName = Config::Parser::toupper(formatName);
			auto it = std::find_if(m_allowedFormats.begin(), m_allowedFormats.end(),
				[&](auto fmt) { return getFormatName(fmt) == formatName; });
			if (it != m_allowedFormats.end())
			{
				if (formats.insert(*it).second)
				{
					valueSet.insert(formatName);
				}
				continue;
			}

			if (m_allowFourCCs && 4 == formatName.length() &&
				formatName.end() == std::find_if(formatName.begin(), formatName.end(), [](char c) { return !std::isalnum(c); }))
			{
				auto fourCC = *reinterpret_cast<const D3DDDIFORMAT*>(formatName.c_str());
				if (formats.insert(fourCC).second)
				{
					valueSet.insert(formatName);
				}
				continue;
			}

			throw ParsingError("invalid format name: '" + formatName + "'");
		}

		m_formats = formats;

		m_valueStr.clear();
		for (const auto& value : valueSet)
		{
			m_valueStr += ", " + value;
		}
		m_valueStr = m_valueStr.substr(2);
	}
}
