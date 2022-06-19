#pragma once

#include <ddraw.h>

#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>

namespace DDraw
{
	class Surface;

	class RealPrimarySurface
	{
	public:
		template <typename DirectDraw>
		static HRESULT create(CompatRef<DirectDraw> dd);

		static void destroyDefaultPrimary();
		static HRESULT flip(CompatPtr<IDirectDrawSurface7> surfaceTargetOverride, DWORD flags);
		static int flush();
		static HWND getDevicePresentationWindow();
		static HWND getPresentationWindow();
		static HRESULT getGammaRamp(DDGAMMARAMP* rampData);
		static RECT getMonitorRect();
		static CompatWeakPtr<IDirectDrawSurface7> getSurface();
		static HWND getTopmost();
		static void init();
		static bool isFullscreen();
		static bool isLost();
		static void release();
		static HRESULT restore();
		static void scheduleUpdate();
		static HRESULT setGammaRamp(DDGAMMARAMP* rampData);
		static void setUpdateReady();
		static void updateDevicePresentationWindowPos();
		static bool waitForFlip(CompatWeakPtr<IDirectDrawSurface7> surface);
		static void waitForFlipFpsLimit();
	};
}
