#include <algorithm>
#include <sstream>

#include <Config/FormatListSetting.h>
#include <Config/Parser.h>
#include <D3dDdi/Log/CommonLog.h>

namespace
{
	void append(std::vector<D3DDDIFORMAT>& formats, D3DDDIFORMAT format)
	{
		if (std::find(formats.begin(), formats.end(), format) == formats.end())
		{
			formats.push_back(format);
		}
	}

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
		const std::map<std::string, std::vector<D3DDDIFORMAT>>& allowedGroups,
		bool allowFourCCs)
		: ListSetting(name, default)
		, m_allowedFormats(allowedFormats)
		, m_allowedGroups(allowedGroups)
		, m_allowFourCCs(allowFourCCs)
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

		if (1 == values.size() && "all" == values[0])
		{
			m_formats.clear();
			return;
		}

		std::vector<D3DDDIFORMAT> formats;
		for (auto formatName : values)
		{
			if ("all" == formatName)
			{
				throw ParsingError("'all' cannot be combined with other values");
			}

			auto group = m_allowedGroups.find(formatName);
			if (group != m_allowedGroups.end())
			{
				for (auto fmt : group->second)
				{
					append(formats, fmt);
				}
				continue;
			}

			formatName = Config::Parser::toupper(formatName);
			auto it = std::find_if(m_allowedFormats.begin(), m_allowedFormats.end(),
				[&](auto fmt) { return getFormatName(fmt) == formatName; });
			if (it != m_allowedFormats.end())
			{
				append(formats, *it);
				continue;
			}

			if (m_allowFourCCs && 4 == formatName.length() &&
				formatName.end() == std::find_if(formatName.begin(), formatName.end(), [](char c) { return !std::isalnum(c); }))
			{
				append(formats, *reinterpret_cast<const D3DDDIFORMAT*>(formatName.c_str()));
				continue;
			}

			throw ParsingError("invalid format name: '" + formatName + "'");
		}

		m_formats = formats;
	}
}
