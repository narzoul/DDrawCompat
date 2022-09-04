#pragma once

#include <d3d.h>

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class SoftwareDevice : public MappedSetting<const IID*>
		{
		public:
			SoftwareDevice()
				: MappedSetting("SoftwareDevice", "rgb", {
					{"app", nullptr},
					{"hal", &IID_IDirect3DHALDevice},
					{"ref", &IID_IDirect3DRefDevice},
					{"rgb", &IID_IDirect3DRGBDevice}
					})
			{
			}
		};
	}
}
