#pragma once

#include <ddraw.h>

#include <Common/CompatWeakPtr.h>

namespace D3dDdi
{
	class Adapter;
	class Resource;

	class SurfaceRepository
	{
	public:
		Resource* getPaletteBltRenderTarget(DWORD width, DWORD height);
		Resource* getPaletteTexture();

		static SurfaceRepository& get(const Adapter& adapter);

	private:
		struct Surface
		{
			CompatWeakPtr<IDirectDrawSurface7> surface;
			Resource* resource;
		};

		SurfaceRepository(const Adapter& adapter);

		CompatWeakPtr<IDirectDrawSurface7> createSurface(DWORD width, DWORD height, const DDPIXELFORMAT& pf, DWORD caps);
		Resource* getResource(Surface& surface, DWORD width, DWORD height, const DDPIXELFORMAT& pf, DWORD caps);

		const Adapter& m_adapter;
		Surface m_paletteBltRenderTarget;
		Surface m_paletteTexture;
	};
}
