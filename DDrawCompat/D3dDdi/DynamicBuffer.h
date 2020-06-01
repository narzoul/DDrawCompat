#pragma once

#include <functional>
#include <memory>

#include <d3d.h>
#include <d3dumddi.h>

namespace D3dDdi
{
	class Device;

	class DynamicBuffer
	{
	public:
		UINT getSize() const { return m_size; }
		INT load(const void* src, UINT count);
		void resize(UINT size);

		operator HANDLE() const { return m_resource.get(); }

	protected:
		DynamicBuffer(HANDLE device, const D3DDDI_DEVICEFUNCS& origVtable, UINT size,
			D3DDDIFORMAT format, D3DDDI_RESOURCEFLAGS resourceFlag);

		void* lock(UINT size);
		void setStride(UINT stride);
		void unlock();

		HANDLE m_device;
		const D3DDDI_DEVICEFUNCS& m_origVtable;
		std::unique_ptr<void, std::function<void(HANDLE)>> m_resource;
		UINT m_size;
		D3DDDIFORMAT m_format;
		D3DDDI_RESOURCEFLAGS m_resourceFlag;
		UINT m_stride;
		UINT m_pos;
	};

	class DynamicIndexBuffer : public DynamicBuffer
	{
	public:
		DynamicIndexBuffer(Device& device, UINT size);
	};

	class DynamicVertexBuffer : public DynamicBuffer
	{
	public:
		DynamicVertexBuffer(Device& device, UINT size);

		using DynamicBuffer::setStride;
	};
}
