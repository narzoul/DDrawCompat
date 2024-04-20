#pragma once

#include <ddraw.h>

#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>
#include <DDraw/Surfaces/Surface.h>
#include <Win32/DisplayMode.h>

namespace DDraw
{
	class PrimarySurface : public Surface
	{
	public:
		PrimarySurface(DWORD origFlags, DWORD origCaps) : Surface(origFlags, origCaps) {}
		virtual ~PrimarySurface();

		template <typename TDirectDraw, typename TSurface, typename TSurfaceDesc>
		static HRESULT create(CompatRef<TDirectDraw> dd, TSurfaceDesc desc, TSurface*& surface);

		static HRESULT flipToGdiSurface();
		static CompatPtr<IDirectDrawSurface7> getGdiSurface();
		static CompatPtr<IDirectDrawSurface7> getBackBuffer();
		static CompatPtr<IDirectDrawSurface7> getLastSurface();
		static const Win32::DisplayMode::MonitorInfo& getMonitorInfo();
		static CompatWeakPtr<IDirectDrawSurface7> getPrimary();
		static HANDLE getFrontResource();
		static HANDLE getGdiResource();
		static DWORD getOrigCaps();
		static void onLost();
		static void setAsRenderTarget();
		static void updatePalette();

		template <typename TSurface>
		static bool isGdiSurface(TSurface* surface);

		static void updateFrontResource();
		static void waitForIdle();

		virtual void restore();

		static CompatWeakPtr<IDirectDrawPalette> s_palette;

	private:
		virtual void createImpl() override;
	};
}
