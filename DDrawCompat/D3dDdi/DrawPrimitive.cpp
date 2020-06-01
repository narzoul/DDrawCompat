#include <algorithm>
#include <set>

#include <Common/Log.h>
#include <D3dDdi/DrawPrimitive.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/Resource.h>
#include <D3dDdi/ScopedCriticalSection.h>

namespace
{
	const UINT INDEX_BUFFER_SIZE = 256 * 1024;
	const UINT VERTEX_BUFFER_SIZE = 1024 * 1024;

	class VertexRhwFixer
	{
	public:
		VertexRhwFixer(D3DTLVERTEX* vertex)
		{
			if (vertex && 0.0f == vertex->rhw)
			{
				vertex->rhw = 1.0f;
				m_vertex = vertex;
			}
			else
			{
				m_vertex = nullptr;
			}
		}

		VertexRhwFixer(const void* vertex)
			: VertexRhwFixer(static_cast<D3DTLVERTEX*>(const_cast<void*>(vertex)))
		{
		}

		~VertexRhwFixer()
		{
			if (m_vertex)
			{
				m_vertex->rhw = 0.0f;
			}
		}

	private:
		D3DTLVERTEX* m_vertex;
	};

	UINT getVertexCount(D3DPRIMITIVETYPE primitiveType, UINT primitiveCount)
	{
		switch (primitiveType)
		{
		case D3DPT_POINTLIST:
			return primitiveCount;
		case D3DPT_LINELIST:
			return primitiveCount * 2;
		case D3DPT_LINESTRIP:
			return primitiveCount + 1;
		case D3DPT_TRIANGLELIST:
			return primitiveCount * 3;
		case D3DPT_TRIANGLESTRIP:
		case D3DPT_TRIANGLEFAN:
			return primitiveCount + 2;
		}
		return 0;
	}
}

namespace D3dDdi
{
	DrawPrimitive::DrawPrimitive(Device& device)
		: m_device(device)
		, m_origVtable(device.getOrigVtable())
		, m_vertexBuffer(device, VERTEX_BUFFER_SIZE)
		, m_indexBuffer(device, m_vertexBuffer ? INDEX_BUFFER_SIZE : 0)
		, m_streamSource{}
	{
		LOG_ONCE("Dynamic vertex buffers are " << (m_vertexBuffer ? "" : "not ") << "available");
		LOG_ONCE("Dynamic index buffers are " << (m_indexBuffer ? "" : "not ") << "available");

		if (m_indexBuffer)
		{
			D3DDDIARG_SETINDICES si = {};
			si.hIndexBuffer = m_indexBuffer;
			si.Stride = 2;
			m_origVtable.pfnSetIndices(m_device, &si);
		}
	}

	void DrawPrimitive::addSysMemVertexBuffer(HANDLE resource, BYTE* vertices, UINT fvf)
	{
		m_sysMemVertexBuffers[resource] = { vertices, fvf };
	}

	HRESULT DrawPrimitive::draw(const D3DDDIARG_DRAWPRIMITIVE& data, const UINT* flagBuffer)
	{
		auto firstVertexPtr = m_streamSource.vertices + data.VStart * m_streamSource.stride;
		VertexRhwFixer fixer((m_streamSource.fvf & D3DFVF_XYZRHW) ? firstVertexPtr : nullptr);

		if (m_streamSource.vertices && m_vertexBuffer)
		{
			auto vertexCount = getVertexCount(data.PrimitiveType, data.PrimitiveCount);
			auto baseVertexIndex = loadVertices(firstVertexPtr, vertexCount);
			if (baseVertexIndex >= 0)
			{
				D3DDDIARG_DRAWPRIMITIVE dp = data;
				dp.VStart = baseVertexIndex;
				return m_origVtable.pfnDrawPrimitive(m_device, &dp, flagBuffer);
			}
		}

		return m_origVtable.pfnDrawPrimitive(m_device, &data, flagBuffer);
	}

	HRESULT DrawPrimitive::drawIndexed(
		D3DDDIARG_DRAWINDEXEDPRIMITIVE2 data, const void* indices, const UINT* flagBuffer)
	{
		auto firstIndexPtr = reinterpret_cast<const UINT16*>(static_cast<const BYTE*>(indices) + data.StartIndexOffset);
		auto indexCount = getVertexCount(data.PrimitiveType, data.PrimitiveCount);
		auto [min, max] = std::minmax_element(firstIndexPtr, firstIndexPtr + indexCount);
		data.MinIndex = *min;
		data.NumVertices = *max - *min + 1;

		auto firstVertexPtr = m_streamSource.vertices + data.BaseVertexOffset + data.MinIndex * m_streamSource.stride;
		VertexRhwFixer fixer((m_streamSource.fvf & D3DFVF_XYZRHW) ? firstVertexPtr : nullptr);

		if (m_streamSource.vertices && m_vertexBuffer)
		{
			auto baseVertexIndex = loadVertices(firstVertexPtr, data.NumVertices);
			if (baseVertexIndex >= 0)
			{
				baseVertexIndex -= data.MinIndex;
				data.BaseVertexOffset = baseVertexIndex * static_cast<INT>(m_streamSource.stride);
			}
		}

		if ((!m_streamSource.vertices || m_vertexBuffer) && m_indexBuffer && !flagBuffer)
		{
			auto startIndex = loadIndices(firstIndexPtr, indexCount);
			if (startIndex >= 0)
			{
				D3DDDIARG_DRAWINDEXEDPRIMITIVE dp = {};
				dp.PrimitiveType = data.PrimitiveType;
				dp.BaseVertexIndex = data.BaseVertexOffset / static_cast<INT>(m_streamSource.stride);
				dp.MinIndex = data.MinIndex;
				dp.NumVertices = data.NumVertices;
				dp.StartIndex = startIndex;
				dp.PrimitiveCount = data.PrimitiveCount;
				return m_origVtable.pfnDrawIndexedPrimitive(m_device, &dp);
			}
		}

		return m_origVtable.pfnDrawIndexedPrimitive2(m_device, &data, 2, indices, flagBuffer);
	}

	INT DrawPrimitive::loadIndices(const void* indices, UINT count)
	{
		INT startIndex = m_indexBuffer.load(indices, count);
		if (startIndex >= 0)
		{
			return startIndex;
		}

		LOG_ONCE("WARN: Dynamic index buffer lock failed");
		m_indexBuffer.resize(0);
		return -1;
	}

	INT DrawPrimitive::loadVertices(const void* vertices, UINT count)
	{
		UINT size = count * m_streamSource.stride;
		if (size > m_vertexBuffer.getSize())
		{
			m_vertexBuffer.resize((size + VERTEX_BUFFER_SIZE - 1) / VERTEX_BUFFER_SIZE * VERTEX_BUFFER_SIZE);
			if (m_vertexBuffer)
			{
				D3DDDIARG_SETSTREAMSOURCE ss = {};
				ss.hVertexBuffer = m_vertexBuffer;
				ss.Stride = m_streamSource.stride;
				m_origVtable.pfnSetStreamSource(m_device, &ss);
			}
			else
			{
				LOG_ONCE("WARN: Dynamic vertex buffer resize failed");
			}
		}

		if (m_vertexBuffer)
		{
			INT baseVertexIndex = m_vertexBuffer.load(vertices, count);
			if (baseVertexIndex >= 0)
			{
				return baseVertexIndex;
			}
			LOG_ONCE("WARN: Dynamic vertex buffer lock failed");
		}

		D3DDDIARG_SETSTREAMSOURCEUM ss = {};
		ss.Stride = m_streamSource.stride;
		m_origVtable.pfnSetStreamSourceUm(m_device, &ss, m_streamSource.vertices);

		m_vertexBuffer.resize(0);
		m_indexBuffer.resize(0);
		return -1;
	}

	void DrawPrimitive::removeSysMemVertexBuffer(HANDLE resource)
	{
		m_sysMemVertexBuffers.erase(resource);
	}

	HRESULT DrawPrimitive::setStreamSource(const D3DDDIARG_SETSTREAMSOURCE& data)
	{
		auto it = m_sysMemVertexBuffers.find(data.hVertexBuffer);
		if (it != m_sysMemVertexBuffers.end())
		{
			return setSysMemStreamSource(it->second.vertices, data.Stride, it->second.fvf);
		}

		HRESULT result = m_origVtable.pfnSetStreamSource(m_device, &data);
		if (SUCCEEDED(result))
		{
			m_streamSource = { nullptr, data.Stride, 0 };
		}
		return result;
	}

	HRESULT DrawPrimitive::setStreamSourceUm(const D3DDDIARG_SETSTREAMSOURCEUM& data, const void* umBuffer)
	{
		return setSysMemStreamSource(static_cast<const BYTE*>(umBuffer), data.Stride, 0);
	}

	HRESULT DrawPrimitive::setSysMemStreamSource(const BYTE* vertices, UINT stride, UINT fvf)
	{
		HRESULT result = S_OK;
		if (m_vertexBuffer)
		{
			if (!m_streamSource.vertices || stride != m_streamSource.stride)
			{
				D3DDDIARG_SETSTREAMSOURCE ss = {};
				ss.hVertexBuffer = m_vertexBuffer;
				ss.Stride = stride;
				result = m_origVtable.pfnSetStreamSource(m_device, &ss);
				if (SUCCEEDED(result))
				{
					m_vertexBuffer.setStride(stride);
				}
			}
		}
		else if (vertices != m_streamSource.vertices || stride != m_streamSource.stride)
		{
			D3DDDIARG_SETSTREAMSOURCEUM ss = {};
			ss.Stride = stride;
			result = m_origVtable.pfnSetStreamSourceUm(m_device, &ss, vertices);
		}

		if (SUCCEEDED(result))
		{
			m_streamSource = { vertices, stride, fvf };
		}
		return result;
	}
}
