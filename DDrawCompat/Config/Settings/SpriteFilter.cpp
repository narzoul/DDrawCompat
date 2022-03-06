#include <Config/Settings/SpriteFilter.h>
#include <D3dDdi/DeviceState.h>

namespace Config
{
	namespace Settings
	{
		SpriteFilter::SpriteFilter()
			: MappedSetting("SpriteFilter", "app", {
				{"app", D3DTEXF_NONE},
				{"point", D3DTEXF_POINT},
				{"linear", D3DTEXF_LINEAR}
				})
		{
		}
	}
}
