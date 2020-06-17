#pragma once

#include <map>
#include <unordered_map>

#include <d3d.h>
#include <d3dnthal.h>
#include <d3dumddi.h>

#include <D3dDdi/DeviceState.h>
#include <D3dDdi/DrawPrimitive.h>

namespace D3dDdi
{
	class Adapter;
	class Resource;

	class Device
	{
	public:
		Device(HANDLE adapter, HANDLE device);

		Device(const Device&) = delete;
		Device(Device&&) = delete;
		Device& operator=(const Device&) = delete;
		Device& operator=(Device&&) = delete;

		operator HANDLE() const { return m_device; }

		HRESULT blt(const D3DDDIARG_BLT* data);
		HRESULT clear(const D3DDDIARG_CLEAR* data, UINT numRect, const RECT* rect);
		HRESULT colorFill(const D3DDDIARG_COLORFILL* data);
		HRESULT createResource(D3DDDIARG_CREATERESOURCE* data);
		HRESULT createResource2(D3DDDIARG_CREATERESOURCE2* data);
		HRESULT destroyResource(HANDLE resource);
		HRESULT drawIndexedPrimitive2(const D3DDDIARG_DRAWINDEXEDPRIMITIVE2* data,
			UINT indicesSize, const void* indexBuffer, const UINT* flagBuffer);
		HRESULT drawPrimitive(const D3DDDIARG_DRAWPRIMITIVE* data, const UINT* flagBuffer);
		HRESULT flush();
		HRESULT flush1(UINT FlushFlags);
		HRESULT lock(D3DDDIARG_LOCK* data);
		HRESULT openResource(D3DDDIARG_OPENRESOURCE* data);
		HRESULT present(const D3DDDIARG_PRESENT* data);
		HRESULT present1(D3DDDIARG_PRESENT1* data);
		HRESULT setRenderTarget(const D3DDDIARG_SETRENDERTARGET* data);
		HRESULT setStreamSource(const D3DDDIARG_SETSTREAMSOURCE* data);
		HRESULT setStreamSourceUm(const D3DDDIARG_SETSTREAMSOURCEUM* data, const void* umBuffer);
		HRESULT unlock(const D3DDDIARG_UNLOCK* data);

		Adapter& getAdapter() const { return m_adapter; }
		const D3DDDI_DEVICEFUNCS& getOrigVtable() const { return m_origVtable; }
		Resource* getResource(HANDLE resource);
		DeviceState& getState() { return m_state; }

		void flushPrimitives() { m_drawPrimitive.flushPrimitives(); }
		void prepareForRendering(HANDLE resource, UINT subResourceIndex, bool isReadOnly);
		void prepareForRendering();

		static void add(HANDLE adapter, HANDLE device);
		static Device& get(HANDLE device);
		static void remove(HANDLE device);

		static void enableFlush(bool enable) { s_isFlushEnabled = enable; }
		static Resource* findResource(HANDLE resource);
		static Resource* getGdiResource();
		static void setGdiResourceHandle(HANDLE resource);
		static void setReadOnlyGdiLock(bool enable);

	private:
		template <typename Arg>
		HRESULT createResourceImpl(Arg& data);

		const D3DDDI_DEVICEFUNCS& m_origVtable;
		Adapter& m_adapter;
		HANDLE m_device;
		std::unordered_map<HANDLE, Resource> m_resources;
		Resource* m_renderTarget;
		UINT m_renderTargetSubResourceIndex;
		HANDLE m_sharedPrimary;
		DrawPrimitive m_drawPrimitive;
		DeviceState m_state;

		static std::map<HANDLE, Device> s_devices;
		static bool s_isFlushEnabled;
	};
}
