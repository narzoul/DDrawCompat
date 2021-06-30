#include <Config/ListSetting.h>
#include <Config/Parser.h>

namespace Config
{
	ListSetting::ListSetting(const std::string& name, const std::string& default)
		: Setting(name, default)
	{
	}

	void ListSetting::setValue(const std::string& value)
	{
		std::vector<std::string> values;
		std::size_t startPos = 0;
		std::size_t endPos = 0;

		do
		{
			endPos = value.find(',', startPos);
			if (endPos == std::string::npos)
			{
				endPos = value.length();
			}
			values.push_back(Parser::trim(value.substr(startPos, endPos - startPos)));
			startPos = endPos + 1;
		} while (endPos < value.length());

		setValues(values);
	}
}
