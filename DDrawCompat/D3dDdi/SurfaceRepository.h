#pragma once

#include <array>
#include <functional>
#include <list>
#include <map>
#include <vector>

typedef long NTSTATUS;

#include <Windows.h>
#include <d3dkmthk.h>
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

		SurfaceRepository(CompatPtr<IDirectDraw7> dd);

		void clearReleasedSurfaces();
		Cursor getCursor(HCURSOR cursor);
		CompatWeakPtr<IDirectDraw7> getDirectDraw() { return m_dd; }
		Resource* getDitherTexture(DWORD size);
		Resource* getLogicalXorTexture();
		Resource* getPaletteTexture();
		Resource* getGammaRampTexture(const D3DDDI_GAMMA_RAMP_RGB256x3x16& gammaRamp, bool forceInit);
		const Surface& getNextRenderTarget(DWORD width, DWORD height, D3DDDIFORMAT format,
			const Resource* currentSrcRt = nullptr, const Resource* currentDstRt = nullptr);
		Surface& getPresentationSourceRtt() { return m_presentationSourceRtt; }
		Surface& getPresentationSourceRtt(DWORD width, DWORD height, D3DDDIFORMAT format);
		Surface& getSurface(Surface& surface, DWORD width, DWORD height,
			D3DDDIFORMAT format, DWORD caps, UINT surfaceCount = 1, DWORD caps2 = 0, bool* isNew = nullptr);
		Surface& getTempSysMemSurface(DWORD width, DWORD height);
		Surface& getTempSurface(Surface& surface, DWORD width, DWORD height,
			D3DDDIFORMAT format, DWORD caps, UINT surfaceCount = 1);
		const Surface& getTempTexture(DWORD width, DWORD height, D3DDDIFORMAT format);
		CompatPtr<IDirectDrawSurface7> getWindowedBackBuffer(DWORD width, DWORD height);
		CompatWeakPtr<IDirectDrawSurface7> getWindowedPrimary();
		CompatPtr<IDirectDrawSurface7> getWindowedSrc(RECT rect);
		void release(Surface& surface);

		static SurfaceRepository& getPrimaryRepo();
		static bool inCreateSurface() { return s_inCreateSurface; }
		static bool isLockResourceEnabled() { return s_isLockResourceEnabled; }
		static void enableSurfaceCheck(bool enable);

	private:
		CompatPtr<IDirectDrawSurface7> createSurface(DWORD width, DWORD height,
			D3DDDIFORMAT format, DWORD caps, DWORD caps2, UINT surfaceCount);
		bool getCursorImage(Surface& surface, HCURSOR cursor, DWORD width, DWORD height, UINT flags);
		Resource* getInitializedResource(Surface& surface, DWORD width, DWORD height, D3DDDIFORMAT format, DWORD caps,
			std::function<void(const DDSURFACEDESC2&)> initFunc, bool forceInit = false);
		bool hasAlpha(CompatRef<IDirectDrawSurface7> surface);
		bool isLost(Surface& surface);

		CompatPtr<IDirectDraw7> m_dd;
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
		Surface m_presentationSourceRtt;
		std::array<Surface, 3> m_renderTargets;
		std::array<Surface, 3> m_hqRenderTargets;
		std::map<D3DDDIFORMAT, Surface> m_textures;
		std::list<CompatPtr<IDirectDrawSurface7>> m_releasedSurfaces;
		Surface m_sysMemSurface;
		Surface m_windowedBackBuffer;
		CompatPtr<IDirectDrawSurface7> m_windowedPrimary;
		
		static unsigned s_inCreateSurface;
		static bool s_isLockResourceEnabled;
	};
}
