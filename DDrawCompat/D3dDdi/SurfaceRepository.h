#pragma once

#include <functional>
#include <map>

#include <ddraw.h>

#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>
#include <DDraw/Comparison.h>

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
			DDPIXELFORMAT pixelFormat = {};
		};

		Cursor getCursor(HCURSOR cursor);
		Resource* getLogicalXorTexture();
		Resource* getPaletteTexture();
		Resource* getGammaRampTexture();
		Surface& getSurface(Surface& surface, DWORD width, DWORD height,
			const DDPIXELFORMAT& pf, DWORD caps, UINT surfaceCount = 1);
		const Surface& getTempRenderTarget(DWORD width, DWORD height, UINT index = 0);
		Surface& getTempSysMemSurface(DWORD width, DWORD height);
		Surface& getTempSurface(Surface& surface, DWORD width, DWORD height,
			const DDPIXELFORMAT& pf, DWORD caps, UINT surfaceCount = 1);
		const Surface& getTempTexture(DWORD width, DWORD height, const DDPIXELFORMAT& pf);
		void release(Surface& surface);

		static SurfaceRepository& get(const Adapter& adapter);
		static bool inCreateSurface() { return s_inCreateSurface; }
		static void enableSurfaceCheck(bool enable);

	private:
		SurfaceRepository(const Adapter& adapter);

		CompatPtr<IDirectDrawSurface7> createSurface(DWORD width, DWORD height,
			const DDPIXELFORMAT& pf, DWORD caps, UINT surfaceCount);
		bool getCursorImage(Surface& surface, HCURSOR cursor, DWORD width, DWORD height, UINT flags);
		Resource* getInitializedResource(Surface& surface, DWORD width, DWORD height, const DDPIXELFORMAT& pf, DWORD caps,
			std::function<void(const DDSURFACEDESC2&)> initFunc);
		bool hasAlpha(CompatRef<IDirectDrawSurface7> surface);
		bool isLost(Surface& surface);

		const Adapter& m_adapter;
		HCURSOR m_cursor;
		SIZE m_cursorSize;
		POINT m_cursorHotspot;
		Surface m_cursorMaskTexture;
		Surface m_cursorColorTexture;
		Surface m_cursorTempTexture;
		Surface m_gammaRampTexture;
		Surface m_logicalXorTexture;
		Surface m_paletteTexture;
		std::vector<Surface> m_renderTargets;
		std::map<DDPIXELFORMAT, Surface> m_textures;
		std::vector<Surface> m_releasedSurfaces;
		Surface m_sysMemSurface;
		
		static bool s_inCreateSurface;
	};
}
