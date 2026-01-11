#include <Config/Parser.h>
#include <Config/Settings/CompatFixes.h>

namespace Config
{
	namespace Settings
	{
		CompatFixes::CompatFixes()
			: ListSetting("CompatFixes", "none")
			, m_fixes{}
		{
		}

		std::string CompatFixes::addValue(const std::string& value)
		{
#define ADD_FIX(fix) \
			if (#fix == value) \
			{ \
				m_fixes.fix = true; \
				return value; \
			}

			ADD_FIX(nodepthblt);
			ADD_FIX(nodepthlock);
			ADD_FIX(nodwmflush);
			ADD_FIX(nohalftone);
			ADD_FIX(nosyslock);
			ADD_FIX(nowindowborders);
			ADD_FIX(unalignedsurfaces);

			throw ParsingError("invalid value: '" + value + "'");
		}

		void CompatFixes::clear()
		{
			m_fixes = {};
		}
	}
}
