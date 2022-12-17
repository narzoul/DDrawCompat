#include <set>

#include <Common/Comparison.h>
#include <Common/CompatPtr.h>
#include <Common/CompatVtable.h>
#include <Config/Settings/SoftwareDevice.h>
#include <D3dDdi/Device.h>
#include <DDraw/DirectDrawSurface.h>
#include <DDraw/LogUsedResourceFormat.h>
#include <DDraw/ScopedThreadLock.h>
#include <DDraw/Surfaces/Surface.h>
#include <Direct3d/Direct3d.h>
#include <Direct3d/Direct3dDevice.h>
#include <Direct3d/Direct3dVertexBuffer.h>
#include <Direct3d/Visitors/Direct3dVtblVisitor.h>

namespace
{
	template <typename TDirect3d, typename TDirectDrawSurface, typename TDirect3dDevice, typename... Params>
	HRESULT STDMETHODCALLTYPE createDevice(
		TDirect3d* This,
		REFCLSID rclsid,
		TDirectDrawSurface* lpDDS,
		TDirect3dDevice** lplpD3DDevice,
		Params... params)
	{
		DDraw::SuppressResourceFormatLogs suppressResourceFormatLogs;
		auto& iid = Direct3d::replaceDevice(rclsid);
		HRESULT result = getOrigVtable(This).CreateDevice(This, iid, lpDDS, lplpD3DDevice, params...);
		if (DDERR_INVALIDOBJECT == result && lpDDS)
		{
			auto surface = DDraw::Surface::getSurface(*lpDDS);
			if (surface)
			{
				surface->setSizeOverride(1, 1);
				result = getOrigVtable(This).CreateDevice(This, iid, lpDDS, lplpD3DDevice, params...);
				surface->setSizeOverride(0, 0);
			}
		}

		if (SUCCEEDED(result))
		{
			if constexpr (std::is_same_v<TDirect3d, IDirect3D7>)
			{
				Direct3d::Direct3dDevice::hookVtable(*(*lplpD3DDevice)->lpVtbl);
			}
			Direct3d::onCreateDevice(iid, *CompatPtr<IDirectDrawSurface7>::from(lpDDS));
		}
		return result;
	}

	HRESULT STDMETHODCALLTYPE createVertexBuffer(
		IDirect3D7* This,
		LPD3DVERTEXBUFFERDESC lpVBDesc,
		LPDIRECT3DVERTEXBUFFER7* lplpD3DVertexBuffer,
		DWORD dwFlags)
	{
		HRESULT result = getOrigVtable(This).CreateVertexBuffer(This, lpVBDesc, lplpD3DVertexBuffer, dwFlags);
		if (SUCCEEDED(result))
		{
			Direct3d::Direct3dVertexBuffer::hookVtable(*(*lplpD3DVertexBuffer)->lpVtbl);
		}
		return result;
	}

	template <typename Vtable>
	constexpr void setCompatVtable(Vtable& vtable)
	{
		if constexpr (!std::is_same_v<Vtable, IDirect3DVtbl>)
		{
			vtable.CreateDevice = &createDevice;
		}

		if constexpr (std::is_same_v<Vtable, IDirect3D7Vtbl>)
		{
			vtable.CreateVertexBuffer = &createVertexBuffer;
		}
	}
}

namespace Direct3d
{
	void onCreateDevice(const IID& iid, IDirectDrawSurface7& surface)
	{
		if (IID_IDirect3DHALDevice == iid || IID_IDirect3DTnLHalDevice == iid)
		{
			auto device = D3dDdi::Device::findDeviceByResource(
				DDraw::DirectDrawSurface::getDriverResourceHandle(surface));
			if (device)
			{
				device->getState().flush();
			}
		}
	}

	const IID& replaceDevice(const IID& iid)
	{
		if (IID_IDirect3DRampDevice != iid &&
			IID_IDirect3DRGBDevice != iid &&
			IID_IDirect3DHALDevice != iid &&
			IID_IDirect3DMMXDevice != iid &&
			IID_IDirect3DRefDevice != iid &&
			IID_IDirect3DNullDevice != iid &&
			IID_IDirect3DTnLHalDevice != iid)
		{
			return iid;
		}

		auto mappedDeviceType = &iid;
		if (Config::softwareDevice.get() &&
			(IID_IDirect3DRampDevice == iid || IID_IDirect3DMMXDevice == iid || IID_IDirect3DRGBDevice == iid))
		{
			mappedDeviceType = Config::softwareDevice.get();
		}

		static std::set<IID> usedDeviceTypes;
		if (usedDeviceTypes.insert(iid).second)
		{
			Compat::Log log(Config::Settings::LogLevel::INFO);
			log << "Using Direct3D device type: " << iid;
			if (iid != *mappedDeviceType)
			{
				log << " (mapped to " << *mappedDeviceType << ')';
			}
		}
		return *mappedDeviceType;
	}

	namespace Direct3d
	{
		template <typename Vtable>
		void hookVtable(const Vtable& vtable)
		{
			CompatVtable<Vtable>::hookVtable<DDraw::ScopedThreadLock>(vtable);
		}

		template void hookVtable(const IDirect3DVtbl&);
		template void hookVtable(const IDirect3D2Vtbl&);
		template void hookVtable(const IDirect3D3Vtbl&);
		template void hookVtable(const IDirect3D7Vtbl&);
	}
}
