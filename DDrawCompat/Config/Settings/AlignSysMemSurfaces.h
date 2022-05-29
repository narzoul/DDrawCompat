#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class AlignSysMemSurfaces : public MappedSetting<UINT>
		{
		public:
			AlignSysMemSurfaces()
				: MappedSetting("AlignSysMemSurfaces", "on", { {"off", 8}, {"on", 0} })
			{
			}
		};
	}
}
