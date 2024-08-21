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
				VBLANKCOUNT,
				VBLANKRATE,
				VBLANKTIME,
				DDIUSAGE,
				GDIOBJECTS,
				DEBUG,
				VALUE_COUNT
			};

			StatsRows()
				: EnumListSetting("StatsRows", "label, presentrate, fliprate, blitcount, lockcount",
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
						"vblankcount",
						"vblankrate",
						"vblanktime",
						"ddiusage",
						"gdiobjects",
						"debug"
					})
			{
			}
		};
	}

	extern Settings::StatsRows statsRows;
}
