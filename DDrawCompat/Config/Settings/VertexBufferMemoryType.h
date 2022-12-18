#pragma once

#include <ddraw.h>

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class VertexBufferMemoryType : public MappedSetting<DWORD>
		{
		public:
			static const DWORD APP = 0;

			VertexBufferMemoryType::VertexBufferMemoryType()
				: MappedSetting("VertexBufferMemoryType", "sysmem", {
					{"app", APP},
					{"sysmem", DDSCAPS_SYSTEMMEMORY},
					{"vidmem", DDSCAPS_VIDEOMEMORY}
					})
			{
			}
		};
	}

	extern Settings::VertexBufferMemoryType vertexBufferMemoryType;
}
