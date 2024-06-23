#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class VertexFixup : public EnumSetting
		{
		public:
			enum Values { CPU, GPU };

			VertexFixup()
				: EnumSetting("VertexFixup", "gpu", { "cpu", "gpu" })
			{
			}
		};
	}

	extern Settings::VertexFixup vertexFixup;
}
