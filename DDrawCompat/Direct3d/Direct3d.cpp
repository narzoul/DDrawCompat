#include <set>

#include <Common/Comparison.h>
#include <Common/CompatPtr.h>
#include <Common/CompatVtable.h>
#include <Config/Settings/CapsPatches.h>
#include <Config/Settings/SoftwareDevice.h>
#include <Config/Settings/SupportedDepthFormats.h>
#include <Config/Settings/SupportedDevices.h>
#include <Config/Settings/VertexBufferMemoryType.h>
#include <D3dDdi/FormatInfo.h>
#include <DDraw/LogUsedResourceFormat.h>
#include <DDraw/ScopedThreadLock.h>
#include <DDraw/Surfaces/Surface.h>
#include <Direct3d/Direct3d.h>
#include <Direct3d/Direct3dDevice.h>
#include <Direct3d/Direct3dVertexBuffer.h>
#include <Direct3d/Visitors/Direct3dVtblVisitor.h>

namespace
{
	struct EnumArgs
	{
		void* callback;
		void* context;
	};

	D3DVERTEXBUFFERDESC g_vbDesc = {};

	template <typename TDirect3d, typename TDirectDrawSurface, typename TDirect3dDevice, typename... Params>
	HRESULT STDMETHODCALLTYPE createDevice(
		TDirect3d* This,
		REFCLSID rclsid,
		TDirectDrawSurface* lpDDS,
		TDirect3dDevice** lplpD3DDevice,
		Params... params)
	{
		if (!Config::supportedDevices.isSupported(rclsid))
		{
			return DDERR_UNSUPPORTED;
		}

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

		if constexpr (std::is_same_v<TDirect3d, IDirect3D7>)
		{
			if (SUCCEEDED(result))
			{
				Direct3d::Direct3dDevice::hookVtable(*(*lplpD3DDevice)->lpVtbl);
			}
		}
		return result;
	}

	template <typename TDirect3d, typename TDirect3dVertexBuffer, typename... Params>
	HRESULT STDMETHODCALLTYPE createVertexBuffer(
		TDirect3d* This,
		LPD3DVERTEXBUFFERDESC lpVBDesc,
		TDirect3dVertexBuffer* lplpD3DVertexBuffer,
		DWORD dwFlags,
		Params... params)
	{
		if (!lpVBDesc)
		{
			return getOrigVtable(This).CreateVertexBuffer(This, lpVBDesc, lplpD3DVertexBuffer, dwFlags, params...);
		}

		auto desc = *lpVBDesc;
		g_vbDesc = desc;

		switch (Config::vertexBufferMemoryType.get())
		{
		case DDSCAPS_SYSTEMMEMORY:
			desc.dwCaps |= D3DVBCAPS_SYSTEMMEMORY;
			break;
		case DDSCAPS_VIDEOMEMORY:
			desc.dwCaps &= ~D3DVBCAPS_SYSTEMMEMORY;
			break;
		}

		HRESULT result = getOrigVtable(This).CreateVertexBuffer(This, &desc, lplpD3DVertexBuffer, dwFlags, params...);
		if constexpr (std::is_same_v<TDirect3d, IDirect3D7>)
		{
			if (SUCCEEDED(result))
			{
				Direct3d::Direct3dVertexBuffer::hookVtable(*(*lplpD3DVertexBuffer)->lpVtbl);
			}
		}

		g_vbDesc = {};
		return result;
	}

	HRESULT CALLBACK enumDevicesCallback(LPSTR lpDeviceDescription, LPSTR lpDeviceName,
		LPD3DDEVICEDESC7 lpD3DDeviceDesc, LPVOID lpContext)
	{
		if (!Config::supportedDevices.isSupported(lpD3DDeviceDesc->deviceGUID))
		{
			return D3DENUMRET_OK;
		}

		auto& args = *static_cast<EnumArgs*>(lpContext);
		auto origCallback = static_cast<LPD3DENUMDEVICESCALLBACK7>(args.callback);
		if (lpD3DDeviceDesc->dwDevCaps & D3DDEVCAPS_TEXTUREVIDEOMEMORY)
		{
			D3DDEVICEDESC7 desc = *lpD3DDeviceDesc;
			Config::capsPatches.applyPatches(desc);
			return origCallback(lpDeviceDescription, lpDeviceName, &desc, args.context);
		}
		return origCallback(lpDeviceDescription, lpDeviceName, lpD3DDeviceDesc, args.context);
	}

	HRESULT CALLBACK enumDevicesCallback(GUID* lpGuid, LPSTR lpDeviceDescription, LPSTR lpDeviceName,
		LPD3DDEVICEDESC lpD3DHWDeviceDesc, LPD3DDEVICEDESC lpD3DHELDeviceDesc, LPVOID lpContext)
	{
		if (lpGuid && !Config::supportedDevices.isSupported(*lpGuid))
		{
			return D3DENUMRET_OK;
		}

		auto& args = *static_cast<EnumArgs*>(lpContext);
		auto origCallback = static_cast<LPD3DENUMDEVICESCALLBACK>(args.callback);
		if (lpD3DHWDeviceDesc->dwDevCaps & D3DDEVCAPS_TEXTUREVIDEOMEMORY)
		{
			D3DDEVICEDESC desc = {};
			memcpy(&desc, lpD3DHWDeviceDesc, lpD3DHWDeviceDesc->dwSize);
			Config::capsPatches.applyPatches(desc);
			return origCallback(lpGuid, lpDeviceDescription, lpDeviceName, &desc, lpD3DHELDeviceDesc, args.context);
		}
		return origCallback(lpGuid, lpDeviceDescription, lpDeviceName, lpD3DHWDeviceDesc, lpD3DHELDeviceDesc, args.context);
	}

	template <typename TDirect3d, typename EnumDevicesCallback>
	HRESULT CALLBACK enumDevices(TDirect3d* This, EnumDevicesCallback lpEnumDevicesCallback, LPVOID lpUserArg)
	{
		if (!This || !lpEnumDevicesCallback)
		{
			getOrigVtable(This).EnumDevices(This, lpEnumDevicesCallback, lpUserArg);
		}
		EnumArgs args = { lpEnumDevicesCallback, lpUserArg };
		return getOrigVtable(This).EnumDevices(This, enumDevicesCallback, &args);
	}

	HRESULT CALLBACK enumZBufferFormatsCallback(LPDDPIXELFORMAT lpDDPixFmt, LPVOID lpContext)
	{
		if (!Config::supportedDepthFormats.isSupported(D3dDdi::getFormat(*lpDDPixFmt)))
		{
			return D3DENUMRET_OK;
		}
		auto& args = *static_cast<EnumArgs*>(lpContext);
		auto origCallback = static_cast<decltype(&enumZBufferFormatsCallback)>(args.callback);
		return origCallback(lpDDPixFmt, args.context);
	}

	template <typename TDirect3d>
	HRESULT STDMETHODCALLTYPE enumZBufferFormats(TDirect3d* This, REFCLSID riidDevice,
		LPD3DENUMPIXELFORMATSCALLBACK lpEnumCallback, LPVOID lpContext)
	{
		if (!This || !lpEnumCallback ||
			IID_IDirect3DHALDevice != riidDevice && IID_IDirect3DTnLHalDevice != riidDevice)
		{
			if (This && lpEnumCallback)
			{
				LOG_ONCE("Using feature: enumerating software depth formats via " << Compat::getTypeName<TDirect3d>());
			}
			return getOrigVtable(This).EnumZBufferFormats(This, riidDevice, lpEnumCallback, lpContext);
		}
		LOG_ONCE("Using feature: enumerating hardware depth formats via " << Compat::getTypeName<TDirect3d>());
		EnumArgs args = { lpEnumCallback, lpContext };
		return getOrigVtable(This).EnumZBufferFormats(This, riidDevice, enumZBufferFormatsCallback, &args);
	}

	template <typename Vtable>
	constexpr void setCompatVtable(Vtable& vtable)
	{
		if constexpr (!std::is_same_v<Vtable, IDirect3DVtbl>)
		{
			vtable.CreateDevice = &createDevice;
		}

		if constexpr (std::is_same_v<Vtable, IDirect3D3Vtbl> || std::is_same_v<Vtable, IDirect3D7Vtbl>)
		{
			vtable.CreateVertexBuffer = &createVertexBuffer;
			vtable.EnumZBufferFormats = &enumZBufferFormats;
		}

		vtable.EnumDevices = &enumDevices;
	}
}

namespace Direct3d
{
	D3DVERTEXBUFFERDESC getVertexBufferDesc()
	{
		return g_vbDesc;
	}

	bool isDeviceType(const IID& iid)
	{
		return IID_IDirect3DRampDevice == iid ||
			IID_IDirect3DRGBDevice == iid ||
			IID_IDirect3DHALDevice == iid ||
			IID_IDirect3DMMXDevice == iid ||
			IID_IDirect3DRefDevice == iid ||
			IID_IDirect3DNullDevice == iid ||
			IID_IDirect3DTnLHalDevice == iid;
	}

	const IID& replaceDevice(const IID& iid)
	{
		if (!isDeviceType(iid))
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
