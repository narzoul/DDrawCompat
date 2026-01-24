#pragma once

#include <ddraw.h>

#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>
#include <Config/AtomicSetting.h>

namespace DDraw
{
	class Surface;

	class RealPrimarySurface
	{
	public:
		static HRESULT create(CompatRef<IDirectDraw> dd);
		static void flip(CompatPtr<IDirectDrawSurface7> surfaceTargetOverride, DWORD flags);
		static int flush();
		static Config::AtomicSetting getFpsLimiter();
		static HRESULT getGammaRamp(DDGAMMARAMP* rampData);
		static HWND getPresentationWindow();
		static CompatWeakPtr<IDirectDrawSurface7> getSurface();
		static HWND getTopmost();
		static void init();
		static bool isExclusiveFullscreen();
		static bool isFullscreen();
		static bool isLost();
		static bool isProcessActive();
		static void release();
		static HRESULT restore();
		static void scheduleOverlayUpdate();
		static void scheduleUpdate(bool allowFlush = false);
		static HRESULT setGammaRamp(DDGAMMARAMP* rampData);
		static void setPresentationWindowTopmost();
		static void setUpdateReady();
		static void suppressLost(bool suppress);
		static void updateFpsLimiter();
		static void waitForFlip(CompatWeakPtr<IDirectDrawSurface7> surface);
		static void waitForFlipFpsLimit(unsigned fpsLimit, bool doFlush = true);
	};
}
