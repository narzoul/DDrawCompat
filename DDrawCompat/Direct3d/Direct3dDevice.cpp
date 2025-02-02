#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>
#include <Common/CompatVtable.h>
#include <Common/Log.h>
#include <Config/Settings/CapsPatches.h>
#include <Config/Settings/PalettizedTextures.h>
#include <Config/Settings/SupportedTextureFormats.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/FormatInfo.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <DDraw/ScopedThreadLock.h>
#include <DDraw/Surfaces/Surface.h>
#include <Direct3d/Direct3dDevice.h>
#include <Direct3d/Visitors/Direct3dDeviceVtblVisitor.h>

namespace
{
	struct EnumTextureFormatsArgs
	{
		void* callback;
		void* context;
	};

	bool isSupported(const DDPIXELFORMAT& pf)
	{
		if ((pf.dwFlags & DDPF_FOURCC) && pf.dwFourCC < 0x100)
		{
			// D3DDDIFMT_A8B8G8R8 and D3DDDIFMT_X8B8G8R8 are enumerated like this, but nobody is expected to use them,
			// and with proper pixel formats these cannot be created in video memory anyway.
			return false;
		}
		if ((pf.dwFlags & DDPF_PALETTEINDEXED8) && !Config::palettizedTextures.get())
		{
			return false;
		}
		return Config::supportedTextureFormats.isSupported(D3dDdi::getFormat(pf));
	}

	bool isSupported(const DDSURFACEDESC& desc)
	{
		return isSupported(desc.ddpfPixelFormat);
	}

	template <typename Format>
	HRESULT CALLBACK enumTextureFormatsCallback(Format* lpFormat, LPVOID lpContext)
	{
		if (!isSupported(*lpFormat))
		{
			return D3DENUMRET_OK;
		}
		auto& args = *static_cast<EnumTextureFormatsArgs*>(lpContext);
		auto origCallback = static_cast<decltype(&enumTextureFormatsCallback<Format>)>(args.callback);
		return origCallback(lpFormat, args.context);
	}

	template <typename TDirect3DDevice>
	D3DDEVICEDESC getCaps(TDirect3DDevice* This)
	{
		D3DDEVICEDESC hwDesc = {};
		hwDesc.dwSize = sizeof(hwDesc);
		D3DDEVICEDESC helDesc = {};
		helDesc.dwSize = sizeof(helDesc);
		getOrigVtable(This).GetCaps(This, &hwDesc, &helDesc);
		return hwDesc;
	}

	D3DDEVICEDESC7 getCaps(IDirect3DDevice7* This)
	{
		D3DDEVICEDESC7 desc = {};
		getOrigVtable(This).GetCaps(This, &desc);
		return desc;
	}

	template <typename TDirect3DDevice>
	bool isHalDevice(TDirect3DDevice* This)
	{
		return getCaps(This).dwDevCaps & D3DDEVCAPS_TEXTUREVIDEOMEMORY;
	}

	template <typename TDirect3DDevice, typename EnumProc>
	HRESULT STDMETHODCALLTYPE enumTextureFormats(TDirect3DDevice* This, EnumProc* lpd3dEnumPixelProc, LPVOID lpArg)
	{
		if (!This || !lpd3dEnumPixelProc || !isHalDevice(This))
		{
			if (This && lpd3dEnumPixelProc)
			{
				LOG_ONCE("Using feature: enumerating software texture formats via " << Compat::getTypeName<TDirect3DDevice>());
			}
			return getOrigVtable(This).EnumTextureFormats(This, lpd3dEnumPixelProc, lpArg);
		}
		LOG_ONCE("Using feature: enumerating hardware texture formats via " << Compat::getTypeName<TDirect3DDevice>());
		EnumTextureFormatsArgs args = { lpd3dEnumPixelProc, lpArg };
		return getOrigVtable(This).EnumTextureFormats(This, enumTextureFormatsCallback, &args);
	}

	void dumpExecuteBuffer(LPDIRECT3DEXECUTEBUFFER lpDirect3DExecuteBuffer)
	{
		if (Config::logLevel.get() < Config::Settings::LogLevel::TRACE || !lpDirect3DExecuteBuffer)
		{
			return;
		}

		D3DEXECUTEDATA data = {};
		data.dwSize = sizeof(data);
		if (FAILED(getOrigVtable(lpDirect3DExecuteBuffer).GetExecuteData(lpDirect3DExecuteBuffer, &data)))
		{
			return;
		}

		D3DEXECUTEBUFFERDESC desc = {};
		desc.dwSize = sizeof(desc);
		if (FAILED(getOrigVtable(lpDirect3DExecuteBuffer).Lock(lpDirect3DExecuteBuffer, &desc)))
		{
			return;
		}

		getOrigVtable(lpDirect3DExecuteBuffer).Unlock(lpDirect3DExecuteBuffer);

		LOG_TRACE << data;

		auto buf = static_cast<const BYTE*>(desc.lpData);
		LOG_TRACE << Compat::array(reinterpret_cast<const D3DTLVERTEX*>(buf + data.dwVertexOffset), data.dwVertexCount);

		auto currPtr = buf + data.dwInstructionOffset;
		auto endPtr = currPtr + data.dwInstructionLength;
		while (currPtr < endPtr)
		{
			auto inst = reinterpret_cast<const D3DINSTRUCTION*>(currPtr);
			LOG_TRACE << *inst;
			currPtr += sizeof(D3DINSTRUCTION) + inst->bSize * inst->wCount;
		}
	}

	HRESULT STDMETHODCALLTYPE execute(IDirect3DDevice* This,
		LPDIRECT3DEXECUTEBUFFER lpDirect3DExecuteBuffer, LPDIRECT3DVIEWPORT lpDirect3DViewport, DWORD dwFlags)
	{
		D3dDdi::ScopedCriticalSection lock;
		dumpExecuteBuffer(lpDirect3DExecuteBuffer);
		D3dDdi::Device::enableFlush(false);
		HRESULT result = getOrigVtable(This).Execute(This, lpDirect3DExecuteBuffer, lpDirect3DViewport, dwFlags);
		D3dDdi::Device::enableFlush(true);
		if (SUCCEEDED(result) && Config::logLevel.get() >= Config::Settings::LogLevel::TRACE)
		{
			D3DEXECUTEDATA data = {};
			data.dwSize = sizeof(data);
			if (SUCCEEDED(getOrigVtable(lpDirect3DExecuteBuffer).GetExecuteData(lpDirect3DExecuteBuffer, &data)))
			{
				LOG_TRACE << data;
			}
		}
		return result;
	}

	HRESULT STDMETHODCALLTYPE getCaps(IDirect3DDevice7* This, LPD3DDEVICEDESC7 lpD3DDevDesc)
	{
		HRESULT result = getOrigVtable(This).GetCaps(This, lpD3DDevDesc);
		if (SUCCEEDED(result) && lpD3DDevDesc && (lpD3DDevDesc->dwDevCaps & D3DDEVCAPS_TEXTUREVIDEOMEMORY))
		{
			Config::capsPatches.applyPatches(*lpD3DDevDesc);
		}
		return result;
	}

	template <typename TDirect3DDevice>
	HRESULT STDMETHODCALLTYPE getCaps(TDirect3DDevice* This, LPD3DDEVICEDESC lpD3DHWDevDesc, LPD3DDEVICEDESC lpD3DHELDevDesc)
	{
		HRESULT result = getOrigVtable(This).GetCaps(This, lpD3DHWDevDesc, lpD3DHELDevDesc);
		if (SUCCEEDED(result) && lpD3DHWDevDesc && (lpD3DHWDevDesc->dwDevCaps & D3DDEVCAPS_TEXTUREVIDEOMEMORY))
		{
			D3DDEVICEDESC desc = {};
			memcpy(&desc, lpD3DHWDevDesc, lpD3DHWDevDesc->dwSize);
			Config::capsPatches.applyPatches(desc);
			memcpy(lpD3DHWDevDesc, &desc, lpD3DHWDevDesc->dwSize);
		}
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

		vtable.EnumTextureFormats = &enumTextureFormats;
		vtable.GetCaps = &getCaps;
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
