#include <algorithm>
#include <vector>

#include "CompatDirectDrawSurface.h"
#include "CompatPrimarySurface.h"
#include "CompatPtr.h"
#include "IReleaseNotifier.h"
#include "RealPrimarySurface.h"

namespace
{
	CompatWeakPtr<IDirectDrawSurface> g_primarySurface = nullptr;
	std::vector<void*> g_primarySurfacePtrs;

	void onRelease()
	{
		Compat::LogEnter("CompatPrimarySurface::onRelease");

		g_primarySurfacePtrs.clear();
		g_primarySurface = nullptr;
		CompatPrimarySurface::palette = nullptr;
		CompatPrimarySurface::width = 0;
		CompatPrimarySurface::height = 0;
		ZeroMemory(&CompatPrimarySurface::paletteEntries, sizeof(CompatPrimarySurface::paletteEntries));
		ZeroMemory(&CompatPrimarySurface::pixelFormat, sizeof(CompatPrimarySurface::pixelFormat));

		RealPrimarySurface::release();

		Compat::LogLeave("CompatPrimarySurface::onRelease");
	}
}

namespace CompatPrimarySurface
{
	DisplayMode getDisplayMode(CompatRef<IDirectDraw7> dd)
	{
		DisplayMode dm = {};
		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		dd->GetDisplayMode(&dd, &desc);
		dm.width = desc.dwWidth;
		dm.height = desc.dwHeight;
		dm.pixelFormat = desc.ddpfPixelFormat;
		dm.refreshRate = desc.dwRefreshRate;
		return dm;
	}

	CompatPtr<IDirectDrawSurface7> getPrimary()
	{
		if (!g_primarySurface)
		{
			return nullptr;
		}
		return CompatPtr<IDirectDrawSurface7>(
			Compat::queryInterface<IDirectDrawSurface7>(g_primarySurface.get()));
	}

	bool isPrimary(void* surface)
	{
		return g_primarySurfacePtrs.end() !=
			std::find(g_primarySurfacePtrs.begin(), g_primarySurfacePtrs.end(), surface);
	}

	void setPrimary(CompatRef<IDirectDrawSurface7> surface)
	{
		CompatPtr<IDirectDrawSurface> surfacePtr(Compat::queryInterface<IDirectDrawSurface>(&surface));
		g_primarySurface = surfacePtr;

		g_primarySurfacePtrs.clear();
		g_primarySurfacePtrs.push_back(&surface);
		g_primarySurfacePtrs.push_back(CompatPtr<IDirectDrawSurface4>(surfacePtr));
		g_primarySurfacePtrs.push_back(CompatPtr<IDirectDrawSurface3>(surfacePtr));
		g_primarySurfacePtrs.push_back(CompatPtr<IDirectDrawSurface2>(surfacePtr));
		g_primarySurfacePtrs.push_back(surfacePtr);

		IReleaseNotifier* releaseNotifierPtr = &releaseNotifier;
		surface->SetPrivateData(&surface, IID_IReleaseNotifier,
			releaseNotifierPtr, sizeof(releaseNotifierPtr), DDSPD_IUNKNOWNPOINTER);
	}

	DisplayMode displayMode = {};
	bool isDisplayModeChanged = false;
	LPDIRECTDRAWPALETTE palette = nullptr;
	PALETTEENTRY paletteEntries[256] = {};
	LONG width = 0;
	LONG height = 0;
	DDPIXELFORMAT pixelFormat = {};
	IReleaseNotifier releaseNotifier(onRelease);
}
