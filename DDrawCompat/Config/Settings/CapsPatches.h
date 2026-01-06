#pragma once

#include <vector>

#include <ddraw.h>
#include <d3d.h>

#include <Config/ListSetting.h>

namespace Config
{
	namespace Settings
	{
		class CapsPatches : public ListSetting
		{
		public:
			enum class Caps { DD, D3D, D3D7 };
			enum class Operation { SET, AND, ANDNOT, OR };

			CapsPatches();

			void applyPatches(DDCAPS& caps) { applyPatches(&caps, Caps::DD); }
			void applyPatches(D3DDEVICEDESC& desc) { applyPatches(&desc, Caps::D3D); }
			void applyPatches(D3DDEVICEDESC7& desc) { applyPatches(&desc, Caps::D3D7); }

		private:
			struct Patch
			{
				Caps caps;
				UINT offset;
				UINT size;
				Operation operation;
				UINT value;
			};

			virtual std::string addValue(const std::string& value) override;
			virtual void clear() override;

			void applyPatches(void* desc, Caps caps);

			std::vector<Patch> m_patches;
		};
	}

	extern Settings::CapsPatches capsPatches;
}
