#pragma once

#include <functional>

#include <ddraw.h>

#include <Common/CompatWeakPtr.h>

namespace D3dDdi
{
	class Adapter;
	class Resource;

	class SurfaceRepository
	{
	public:
		struct Cursor
		{
			HCURSOR cursor;
			SIZE size;
			POINT hotspot;
			Resource* maskTexture;
			Resource* colorTexture;
			Resource* tempTexture;
		};

		struct Surface
		{
			CompatWeakPtr<IDirectDrawSurface7> surface;
			Resource* resource;
			DWORD width;
			DWORD height;
			DDPIXELFORMAT pixelFormat;
		};

		Cursor getCursor(HCURSOR cursor);
		Resource* getLogicalXorTexture();
		Resource* getPaletteTexture();
		const Surface& getRenderTarget(DWORD width, DWORD height);
		Surface& getSurface(Surface& surface, DWORD width, DWORD height,
			const DDPIXELFORMAT& pf, DWORD caps, UINT surfaceCount = 1);

		static SurfaceRepository& get(const Adapter& adapter);
		static bool inCreateSurface() { return s_inCreateSurface; }

	private:
		SurfaceRepository(const Adapter& adapter);

		CompatWeakPtr<IDirectDrawSurface7> createSurface(DWORD width, DWORD height,
			const DDPIXELFORMAT& pf, DWORD caps, UINT surfaceCount);
		Resource* getBitmapResource(Surface& surface, HBITMAP bitmap, const RECT& rect, const DDPIXELFORMAT& pf, DWORD caps);
		Resource* getInitializedResource(Surface& surface, DWORD width, DWORD height, const DDPIXELFORMAT& pf, DWORD caps,
			std::function<void(const DDSURFACEDESC2&)> initFunc);
		bool isLost(Surface& surface);
		void release(Surface& surface);

		const Adapter& m_adapter;
		HCURSOR m_cursor;
		SIZE m_cursorSize;
		POINT m_cursorHotspot;
		Surface m_cursorMaskTexture;
		Surface m_cursorColorTexture;
		Surface m_cursorTempTexture;
		Surface m_logicalXorTexture;
		Surface m_paletteTexture;
		Surface m_renderTarget;
		
		static bool s_inCreateSurface;
	};
}
