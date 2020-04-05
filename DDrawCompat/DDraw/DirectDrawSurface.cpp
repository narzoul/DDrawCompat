#include <set>

#include "DDraw/DirectDrawSurface.h"
#include "DDraw/Surfaces/Surface.h"
#include "DDraw/Surfaces/SurfaceImpl.h"

namespace
{
	struct AddAttachedSurfacesContext
	{
		IDirectDrawSurface7* rootSurface;
		std::vector<CompatPtr<IDirectDrawSurface7>> surfaces;
	};

	HRESULT WINAPI addAttachedSurfaces(
		LPDIRECTDRAWSURFACE7 lpDDSurface, LPDDSURFACEDESC2 /*lpDDSurfaceDesc*/, LPVOID lpContext)
	{
		CompatPtr<IDirectDrawSurface7> surface(lpDDSurface);
		auto& context(*static_cast<AddAttachedSurfacesContext*>(lpContext));
		if (surface == context.rootSurface)
		{
			return DD_OK;
		}
		context.surfaces.push_back(surface);
		surface->EnumAttachedSurfaces(surface, &context, &addAttachedSurfaces);
		return DD_OK;
	}

	template <typename CompatMethod, CompatMethod compatMethod,
		typename OrigMethod, OrigMethod origMethod,
		typename TSurface, typename... Params>
	HRESULT STDMETHODCALLTYPE callImpl(TSurface* This, Params... params)
	{
		DDraw::Surface* surface = This ? DDraw::Surface::getSurface(*This) : nullptr;
		if (!surface || !(surface->getImpl<TSurface>()))
		{
			return (CompatVtable<Vtable<TSurface>>::s_origVtable.*origMethod)(This, params...);
		}
		return (surface->getImpl<TSurface>()->*compatMethod)(This, params...);
	}
}

#define SET_COMPAT_METHOD(method) \
	vtable.method = &callImpl<decltype(&SurfaceImpl<TSurface>::method), &SurfaceImpl<TSurface>::method, \
							  decltype(&Vtable<TSurface>::method), &Vtable<TSurface>::method>

namespace DDraw
{
	std::vector<CompatPtr<IDirectDrawSurface7>> getAllAttachedSurfaces(CompatRef<IDirectDrawSurface7> surface)
	{
		AddAttachedSurfacesContext context = { &surface };
		surface->EnumAttachedSurfaces(&surface, &context, &addAttachedSurfaces);
		return context.surfaces;
	}

	template <typename TSurface>
	void DirectDrawSurface<TSurface>::setCompatVtable(Vtable<TSurface>& vtable)
	{
		SET_COMPAT_METHOD(Blt);
		SET_COMPAT_METHOD(BltFast);
		SET_COMPAT_METHOD(Flip);
		SET_COMPAT_METHOD(GetBltStatus);
		SET_COMPAT_METHOD(GetCaps);
		SET_COMPAT_METHOD(GetDC);
		SET_COMPAT_METHOD(GetFlipStatus);
		SET_COMPAT_METHOD(GetSurfaceDesc);
		SET_COMPAT_METHOD(IsLost);
		SET_COMPAT_METHOD(Lock);
		SET_COMPAT_METHOD(QueryInterface);
		SET_COMPAT_METHOD(ReleaseDC);
		SET_COMPAT_METHOD(Restore);
		SET_COMPAT_METHOD(SetPalette);
		SET_COMPAT_METHOD(Unlock);
	}

	template DirectDrawSurface<IDirectDrawSurface>;
	template DirectDrawSurface<IDirectDrawSurface2>;
	template DirectDrawSurface<IDirectDrawSurface3>;
	template DirectDrawSurface<IDirectDrawSurface4>;
	template DirectDrawSurface<IDirectDrawSurface7>;
}
