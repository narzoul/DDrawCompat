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
		static HRESULT create(CompatRef<IDirectDraw> dd);
		static HRESULT flip(CompatPtr<IDirectDrawSurface7> surfaceTargetOverride, DWORD flags);
		static int flush();
		static HRESULT getGammaRamp(DDGAMMARAMP* rampData);
		static HWND getPresentationWindow();
		static CompatWeakPtr<IDirectDrawSurface7> getSurface();
		static HWND getTopmost();
		static void init();
		static bool isExclusiveFullscreen();
		static bool isFullscreen();
		static bool isLost();
		static void release();
		static HRESULT restore();
		static void scheduleOverlayUpdate();
		static void scheduleUpdate();
		static HRESULT setGammaRamp(DDGAMMARAMP* rampData);
		static void setPresentationWindowTopmost();
		static void setUpdateReady();
		static bool waitForFlip(CompatWeakPtr<IDirectDrawSurface7> surface);
		static void waitForFlipFpsLimit();
	};
}
