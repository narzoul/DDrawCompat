#pragma once

#include <array>
#include <map>
#include <memory>
#include <vector>

#include <d3d.h>
#include <d3dnthal.h>
#include <d3dumddi.h>

#include <D3dDdi/DeviceState.h>
#include <D3dDdi/DrawPrimitive.h>
#include <D3dDdi/ShaderBlitter.h>
#include <D3dDdi/SurfaceRepository.h>

namespace D3dDdi
{
	class Adapter;
	class Resource;

	class Device
	{
	public:
		Device(Adapter& adapter, HANDLE device, HANDLE runtimeDevice);

		Device(const Device&) = delete;
		Device(Device&&) = delete;
		Device& operator=(const Device&) = delete;
		Device& operator=(Device&&) = delete;

		operator HANDLE() const { return m_device; }

		HRESULT pfnBlt(const D3DDDIARG_BLT* data);
		HRESULT pfnClear(const D3DDDIARG_CLEAR* data, UINT numRect, const RECT* rect);
		HRESULT pfnColorFill(const D3DDDIARG_COLORFILL* data);
		HRESULT pfnCreateResource(D3DDDIARG_CREATERESOURCE* data);
		HRESULT pfnCreateResource2(D3DDDIARG_CREATERESOURCE2* data);
		HRESULT pfnCreateVertexShaderFunc(D3DDDIARG_CREATEVERTEXSHADERFUNC* data, const UINT* code);
		HRESULT pfnDepthFill(const D3DDDIARG_DEPTHFILL* data);
		HRESULT pfnDestroyDevice();
		HRESULT pfnDestroyResource(HANDLE resource);
		HRESULT pfnDrawIndexedPrimitive2(const D3DDDIARG_DRAWINDEXEDPRIMITIVE2* data,
			UINT indicesSize, const void* indexBuffer, const UINT* flagBuffer);
		HRESULT pfnDrawPrimitive(const D3DDDIARG_DRAWPRIMITIVE* data, const UINT* flagBuffer);
		HRESULT pfnFlush();
		HRESULT pfnFlush1(UINT FlushFlags);
		HRESULT pfnLock(D3DDDIARG_LOCK* data);
		HRESULT pfnOpenResource(D3DDDIARG_OPENRESOURCE* data);
		HRESULT pfnPresent(const D3DDDIARG_PRESENT* data);
		HRESULT pfnPresent1(D3DDDIARG_PRESENT1* data);
		HRESULT pfnSetPalette(const D3DDDIARG_SETPALETTE* data);
		HRESULT pfnTexBlt(const D3DDDIARG_TEXBLT* data);
		HRESULT pfnTexBlt1(const D3DDDIARG_TEXBLT1* data);
		HRESULT pfnUnlock(const D3DDDIARG_UNLOCK* data);
		HRESULT pfnUpdatePalette(const D3DDDIARG_UPDATEPALETTE* data, const PALETTEENTRY* paletteData);
		HRESULT pfnValidateDevice(D3DDDIARG_VALIDATETEXTURESTAGESTATE* data);

		Adapter& getAdapter() const { return m_adapter; }
		std::pair<UINT, UINT> getColorKeyMethod();
		DrawPrimitive& getDrawPrimitive() { return m_drawPrimitive; }
		const D3DDDI_DEVICEFUNCS& getOrigVtable() const { return m_origVtable; }
		RGBQUAD* getPalette(UINT paletteHandle) { return m_palettes[paletteHandle].data(); }
		SurfaceRepository& getRepo() const { return SurfaceRepository::get(m_adapter); }
		Resource* getResource(HANDLE resource);
		DeviceState& getState() { return m_state; }
		ShaderBlitter& getShaderBlitter() { return m_shaderBlitter; }

		HRESULT createPrivateResource(D3DDDIARG_CREATERESOURCE2& data);
		void flushPrimitives() { m_drawPrimitive.flushPrimitives(); }
		void prepareForGpuWrite();
		void setDepthStencil(HANDLE resource);
		void setRenderTarget(const D3DDDIARG_SETRENDERTARGET& data);
		void updateConfig();

		static void add(Adapter& adapter, HANDLE device, HANDLE runtimeDevice);
		static Device& get(HANDLE device) { return s_devices.find(device)->second; }

		static void enableFlush(bool enable) { s_isFlushEnabled = enable; }
		static Device* findDeviceByRuntimeHandle(HANDLE runtimeDevice);
		static Device* findDeviceByResource(HANDLE resource);
		static Resource* findResource(HANDLE resource);
		static Resource* getGdiResource();
		static void setGdiResourceHandle(HANDLE resource);
		static void updateAllConfig();

	private:
		HRESULT clear(D3DDDIARG_CLEAR data, UINT numRect, const RECT* rect, Resource* resource, DWORD flags);
		UINT detectColorKeyMethod();
		void prepareForTextureBlt(HANDLE dstResource, HANDLE srcResource);
		static void updateAllConfigNow();

		D3DDDI_DEVICEFUNCS m_origVtable;
		Adapter& m_adapter;
		HANDLE m_device;
		HANDLE m_runtimeDevice;
		std::map<HANDLE, std::unique_ptr<Resource>> m_resources;
		Resource* m_depthStencil;
		Resource* m_renderTarget;
		UINT m_renderTargetSubResourceIndex;
		HANDLE m_sharedPrimary;
		DrawPrimitive m_drawPrimitive;
		DeviceState m_state;
		ShaderBlitter m_shaderBlitter;
		std::vector<std::array<RGBQUAD, 256>> m_palettes;
		UINT m_autoColorKeyMethod;

		static std::map<HANDLE, Device> s_devices;
		static bool s_isFlushEnabled;
	};
}
