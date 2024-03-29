#pragma once

#include <array>
#include <functional>
#include <map>
#include <vector>

#include <ddraw.h>

#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>

namespace D3dDdi
{
	class Adapter;
	class Resource;

	class SurfaceRepository
	{
	public:
		struct Cursor
		{
			HCURSOR cursor = nullptr;
			SIZE size = {};
			POINT hotspot = {};
			Resource* maskTexture = nullptr;
			Resource* colorTexture = nullptr;
			Resource* tempTexture = nullptr;
		};

		struct Surface
		{
			CompatPtr<IDirectDrawSurface7> surface;
			Resource* resource = nullptr;
			DWORD width = 0;
			DWORD height = 0;
			D3DDDIFORMAT format = D3DDDIFMT_UNKNOWN;
		};

		SurfaceRepository();

		Cursor getCursor(HCURSOR cursor);
		Resource* getDitherTexture(DWORD size);
		Resource* getLogicalXorTexture();
		Resource* getPaletteTexture();
		Resource* getGammaRampTexture();
		const Surface& getNextRenderTarget(DWORD width, DWORD height, D3DDDIFORMAT format,
			const Resource* currentSrcRt = nullptr, const Resource* currentDstRt = nullptr);
		Surface& getSurface(Surface& surface, DWORD width, DWORD height,
			D3DDDIFORMAT format, DWORD caps, UINT surfaceCount = 1, DWORD caps2 = 0);
		Surface& getTempSysMemSurface(DWORD width, DWORD height);
		Surface& getTempSurface(Surface& surface, DWORD width, DWORD height,
			D3DDDIFORMAT format, DWORD caps, UINT surfaceCount = 1);
		const Surface& getTempTexture(DWORD width, DWORD height, D3DDDIFORMAT format);
		void release(Surface& surface);
		void setAsPrimary();
		void setRepository(CompatWeakPtr<IDirectDraw7> dd) { m_dd = dd; }

		static SurfaceRepository& get(const Adapter& adapter);
		static SurfaceRepository& getPrimary();
		static bool inCreateSurface() { return s_inCreateSurface; }
		static void enableSurfaceCheck(bool enable);

	private:
		CompatPtr<IDirectDrawSurface7> createSurface(DWORD width, DWORD height,
			D3DDDIFORMAT format, DWORD caps, DWORD caps2, UINT surfaceCount);
		bool getCursorImage(Surface& surface, HCURSOR cursor, DWORD width, DWORD height, UINT flags);
		Resource* getInitializedResource(Surface& surface, DWORD width, DWORD height, D3DDDIFORMAT format, DWORD caps,
			std::function<void(const DDSURFACEDESC2&)> initFunc);
		bool hasAlpha(CompatRef<IDirectDrawSurface7> surface);
		bool isLost(Surface& surface);

		CompatWeakPtr<IDirectDraw7> m_dd;
		HCURSOR m_cursor;
		SIZE m_cursorSize;
		POINT m_cursorHotspot;
		Surface m_cursorMaskTexture;
		Surface m_cursorColorTexture;
		Surface m_cursorTempTexture;
		Surface m_ditherTexture;
		Surface m_gammaRampTexture;
		Surface m_logicalXorTexture;
		Surface m_paletteTexture;
		std::array<Surface, 3> m_renderTargets;
		std::array<Surface, 3> m_hqRenderTargets;
		std::map<D3DDDIFORMAT, Surface> m_textures;
		std::vector<Surface> m_releasedSurfaces;
		Surface m_sysMemSurface;
		
		static bool s_inCreateSurface;
	};
}
