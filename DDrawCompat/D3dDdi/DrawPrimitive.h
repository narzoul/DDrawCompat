#pragma once

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <d3d.h>
#include <d3dumddi.h>

#include <D3dDdi/DynamicBuffer.h>

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
		struct StreamSource
		{
			const BYTE* vertices;
			UINT stride;
			UINT fvf;
		};

		struct SysMemVertexBuffer
		{
			BYTE* vertices;
			UINT fvf;
		};

		INT loadIndices(const void* indices, UINT count);
		INT loadVertices(const void* vertices, UINT count);
		HRESULT setSysMemStreamSource(const BYTE* vertices, UINT stride, UINT fvf);

		HANDLE m_device;
		const D3DDDI_DEVICEFUNCS& m_origVtable;
		DynamicVertexBuffer m_vertexBuffer;
		DynamicIndexBuffer m_indexBuffer;
		StreamSource m_streamSource;
		std::map<HANDLE, SysMemVertexBuffer> m_sysMemVertexBuffers;
	};
}
