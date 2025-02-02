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
		, m_allowAll(false)
	{
	}

	std::string FormatListSetting::addValue(const std::string& value)
	{
		if ("all" == value)
		{
			m_formats.clear();
			m_allowAll = true;
			return value;
		}

		auto group = m_allowedGroups.find(value);
		if (group != m_allowedGroups.end())
		{
			if (!m_allowAll)
			{
				m_formats.insert(group->second.begin(), group->second.end());
			}
			return value;
		}

		const std::string formatName = Config::Parser::toupper(value);
		auto it = std::find_if(m_allowedFormats.begin(), m_allowedFormats.end(),
			[&](auto fmt) { return getFormatName(fmt) == formatName; });
		if (it != m_allowedFormats.end())
		{
			if (!m_allowAll)
			{
				m_formats.insert(*it);
			}
			return formatName;
		}

		if (m_allowFourCCs && 4 == formatName.length() &&
			formatName.end() == std::find_if(formatName.begin(), formatName.end(), [](char c) { return !std::isalnum(c); }))
		{
			if (!m_allowAll)
			{
				m_formats.insert(*reinterpret_cast<const D3DDDIFORMAT*>(formatName.c_str()));
			}
			return formatName;
		}

		throw ParsingError("invalid value: '" + value + "'");
	}

	void FormatListSetting::clear()
	{
		m_formats.clear();
		m_allowAll = false;
	}

	bool FormatListSetting::isSupported(D3DDDIFORMAT format) const
	{
		if (m_allowAll)
		{
			return true;
		}
		return std::find(m_formats.begin(), m_formats.end(), format) != m_formats.end();
	}
}
