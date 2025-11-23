#include <Config/Parser.h>
#include <Config/Settings/GdiInterops.h>

namespace Config
{
	namespace Settings
	{
		GdiInterops::GdiInterops()
			: ListSetting("GdiInterops", "all")
			, m_interops{}
		{
		}

		std::string GdiInterops::addValue(const std::string& value)
		{
#define ADD_INTEROP(interop) \
			if (#interop == value) \
			{ \
				m_interops.interop = true; \
				return value; \
			}

			ADD_INTEROP(colors);
			ADD_INTEROP(desktop);
			ADD_INTEROP(dialogs);
			ADD_INTEROP(themes);
			ADD_INTEROP(windows);

			if ("all" == value)
			{
				memset(&m_interops, true, sizeof(m_interops));
				return value;
			}

			throw ParsingError("invalid value: '" + value + "'");
		}

		void GdiInterops::clear()
		{
			m_interops = {};
		}
	}
}
