#pragma once

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <d3d.h>
#include <d3dumddi.h>

namespace D3dDdi
{
	class Device;

	class DrawPrimitive
	{
	public:
		DrawPrimitive(Device& device);

		void addSysMemVertexBuffer(HANDLE resource, BYTE* vertices, UINT fvf);
		void removeSysMemVertexBuffer(HANDLE resource);

		HRESULT draw(const D3DDDIARG_DRAWPRIMITIVE& data, const UINT* flagBuffer);
		HRESULT drawIndexed(D3DDDIARG_DRAWINDEXEDPRIMITIVE2 data, const void* indices, const UINT* flagBuffer);
		HRESULT setStreamSource(const D3DDDIARG_SETSTREAMSOURCE& data);
		HRESULT setStreamSourceUm(const D3DDDIARG_SETSTREAMSOURCEUM& data, const void* umBuffer);

	private:
		class Buffer
		{
		public:
			Buffer(HANDLE device, const D3DDDI_DEVICEFUNCS& origVtable, D3DDDIFORMAT format, UINT size);

			HANDLE getHandle() const;
			UINT load(const void* src, UINT count, UINT stride);

		private:
			void* lock(UINT size);
			bool resize(UINT size);
			void unlock();

			HANDLE m_device = nullptr;
			const D3DDDI_DEVICEFUNCS& m_origVtable;
			std::unique_ptr<void, std::function<void(HANDLE)>> m_resource;
			D3DDDIFORMAT m_format;
			UINT m_initialSize;
			UINT m_size;
			UINT m_stride;
			UINT m_pos;
		};

		struct StreamSource
		{
			BYTE* vertices;
			UINT stride;
			UINT fvf;
		};

		struct SysMemVertexBuffer
		{
			BYTE* vertices;
			UINT fvf;
		};

		HRESULT setStreamSource(BYTE* vertices, UINT stride, UINT fvf);

		HANDLE m_device;
		const D3DDDI_DEVICEFUNCS& m_origVtable;
		Buffer m_vertexBuffer;
		Buffer m_indexBuffer;
		StreamSource m_streamSource;
		std::map<HANDLE, SysMemVertexBuffer> m_sysMemVertexBuffers;
	};
}
