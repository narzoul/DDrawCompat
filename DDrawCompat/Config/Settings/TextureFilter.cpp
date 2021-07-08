#include <Config/Settings/TextureFilter.h>
#include <D3dDdi/DeviceState.h>

namespace Config
{
	namespace Settings
	{
		TextureFilter::TextureFilter()
			: MappedSetting("TextureFilter", "app", {
				{"app", {D3DTEXF_NONE, D3DTEXF_NONE, 1}},
				{"point", {D3DTEXF_POINT, D3DTEXF_POINT, 1}},
				{"bilinear", {D3DTEXF_LINEAR, D3DTEXF_POINT, 1}},
				{"trilinear", {D3DTEXF_LINEAR, D3DTEXF_LINEAR, 1}},
				{"af2x", {D3DTEXF_ANISOTROPIC, D3DTEXF_LINEAR, 2}},
				{"af4x", {D3DTEXF_ANISOTROPIC, D3DTEXF_LINEAR, 4}},
				{"af8x", {D3DTEXF_ANISOTROPIC, D3DTEXF_LINEAR, 8}},
				{"af16x", {D3DTEXF_ANISOTROPIC, D3DTEXF_LINEAR, 16}}
				})
		{
		}
	}
}
