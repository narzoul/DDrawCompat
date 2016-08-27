#include <algorithm>
#include <vector>

#include "CompatPtr.h"
#include "DDraw/CompatPrimarySurface.h"
#include "DDraw/DirectDrawSurface.h"
#include "DDraw/IReleaseNotifier.h"
#include "DDraw/RealPrimarySurface.h"

namespace
{
	void onRelease();

	DDSURFACEDESC2 g_primarySurfaceDesc = {};
	CompatWeakPtr<IDirectDrawSurface> g_primarySurface = nullptr;
	std::vector<void*> g_primarySurfacePtrs;
	DDraw::IReleaseNotifier g_releaseNotifier(onRelease);

	void onRelease()
	{
		Compat::LogEnter("CompatPrimarySurface::onRelease");

		g_primarySurfacePtrs.clear();
		g_primarySurface = nullptr;
		DDraw::CompatPrimarySurface::g_palette = nullptr;
		ZeroMemory(&DDraw::CompatPrimarySurface::g_paletteEntries,
			sizeof(DDraw::CompatPrimarySurface::g_paletteEntries));
		ZeroMemory(&g_primarySurfaceDesc, sizeof(g_primarySurfaceDesc));

		DDraw::RealPrimarySurface::release();

		Compat::LogLeave("CompatPrimarySurface::onRelease");
	}
}

namespace DDraw
{
	namespace CompatPrimarySurface
	{
		const DDSURFACEDESC2& getDesc()
		{
			return g_primarySurfaceDesc;
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

			ZeroMemory(&g_primarySurfaceDesc, sizeof(g_primarySurfaceDesc));
			g_primarySurfaceDesc.dwSize = sizeof(g_primarySurfaceDesc);
			surface->GetSurfaceDesc(&surface, &g_primarySurfaceDesc);

			g_primarySurfacePtrs.clear();
			g_primarySurfacePtrs.push_back(&surface);
			g_primarySurfacePtrs.push_back(CompatPtr<IDirectDrawSurface4>(surfacePtr));
			g_primarySurfacePtrs.push_back(CompatPtr<IDirectDrawSurface3>(surfacePtr));
			g_primarySurfacePtrs.push_back(CompatPtr<IDirectDrawSurface2>(surfacePtr));
			g_primarySurfacePtrs.push_back(surfacePtr);

			IReleaseNotifier* releaseNotifierPtr = &g_releaseNotifier;
			surface->SetPrivateData(&surface, IID_IReleaseNotifier,
				releaseNotifierPtr, sizeof(releaseNotifierPtr), DDSPD_IUNKNOWNPOINTER);
		}

		CompatWeakPtr<IDirectDrawPalette> g_palette;
		PALETTEENTRY g_paletteEntries[256] = {};
	}
}
