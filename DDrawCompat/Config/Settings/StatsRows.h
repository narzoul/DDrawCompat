#pragma once

#include <Config/EnumListSetting.h>

namespace Config
{
	namespace Settings
	{
		class StatsRows : public EnumListSetting
		{
		public:
			enum Values {
				LABEL,
				PRESENTCOUNT,
				PRESENTRATE,
				PRESENTTIME,
				FLIPCOUNT,
				FLIPRATE,
				FLIPTIME,
				BLITCOUNT,
				BLITRATE,
				BLITTIME,
				LOCKCOUNT,
				LOCKRATE,
				LOCKTIME,
				DDIUSAGE,
				GDIOBJECTS
			};

			StatsRows()
				: EnumListSetting("StatsRows", "label, presentrate, fliprate, blitcount, lockcount, ddiusage",
					{
						"label",
						"presentcount",
						"presentrate",
						"presenttime",
						"flipcount",
						"fliprate",
						"fliptime",
						"blitcount",
						"blitrate",
						"blittime",
						"lockcount",
						"lockrate",
						"locktime",
						"ddiusage",
						"gdiobjects"
					})
			{
			}
		};
	}

	extern Settings::StatsRows statsRows;
}
