#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>
#include <Common/CompatVtable.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <DDraw/ScopedThreadLock.h>
#include <DDraw/Surfaces/Surface.h>
#include <Direct3d/Direct3dDevice.h>
#include <Direct3d/Visitors/Direct3dDeviceVtblVisitor.h>

namespace
{
	HRESULT STDMETHODCALLTYPE execute(IDirect3DDevice* This,
		LPDIRECT3DEXECUTEBUFFER lpDirect3DExecuteBuffer, LPDIRECT3DVIEWPORT lpDirect3DViewport, DWORD dwFlags)
	{
		D3dDdi::ScopedCriticalSection lock;
		D3dDdi::Device::enableFlush(false);
		HRESULT result = getOrigVtable(This).Execute(This, lpDirect3DExecuteBuffer, lpDirect3DViewport, dwFlags);
		D3dDdi::Device::enableFlush(true);
		return result;
	}

	template <typename TDirect3DDevice, typename TSurface>
	HRESULT STDMETHODCALLTYPE setRenderTarget(TDirect3DDevice* This, TSurface* lpNewRenderTarget, DWORD dwFlags)
	{
		HRESULT result = getOrigVtable(This).SetRenderTarget(This, lpNewRenderTarget, dwFlags);
		if (DDERR_INVALIDPARAMS == result && lpNewRenderTarget)
		{
			auto surface = DDraw::Surface::getSurface(*lpNewRenderTarget);
			if (surface)
			{
				surface->setSizeOverride(1, 1);
				result = getOrigVtable(This).SetRenderTarget(This, lpNewRenderTarget, dwFlags);
				surface->setSizeOverride(0, 0);
			}
		}
		return result;
	}

	template <typename Vtable>
	constexpr void setCompatVtable(Vtable& vtable)
	{
		if constexpr (std::is_same_v<Vtable, IDirect3DDeviceVtbl>)
		{
			vtable.Execute = &execute;
		}
		else
		{
			vtable.SetRenderTarget = &setRenderTarget;
		}
	}
}

namespace Direct3d
{
	namespace Direct3dDevice
	{
		template <typename Vtable>
		void hookVtable(const Vtable& vtable)
		{
			CompatVtable<Vtable>::hookVtable<DDraw::ScopedThreadLock>(vtable);
		}

		template void hookVtable(const IDirect3DDeviceVtbl&);
		template void hookVtable(const IDirect3DDevice2Vtbl&);
		template void hookVtable(const IDirect3DDevice3Vtbl&);
		template void hookVtable(const IDirect3DDevice7Vtbl&);
	}
}
