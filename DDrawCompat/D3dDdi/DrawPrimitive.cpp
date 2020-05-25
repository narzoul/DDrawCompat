#include <algorithm>
#include <set>

#include <D3dDdi/DrawPrimitive.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/Resource.h>
#include <D3dDdi/ScopedCriticalSection.h>

namespace
{
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

		VertexRhwFixer(void* vertex)
			: VertexRhwFixer(static_cast<D3DTLVERTEX*>(vertex))
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

	UINT roundUpToNearestMultiple(UINT num, UINT mul)
	{
		return (num + mul - 1) / mul * mul;
	}
}

namespace D3dDdi
{
	DrawPrimitive::Buffer::Buffer(HANDLE device, const D3DDDI_DEVICEFUNCS& origVtable, D3DDDIFORMAT format, UINT size)
		: m_device(device)
		, m_origVtable(origVtable)
		, m_resource(nullptr, [=](HANDLE vb) { origVtable.pfnDestroyResource(device, vb); })
		, m_format(format)
		, m_initialSize(size)
		, m_size(0)
		, m_stride(0)
		, m_pos(0)
	{
		resize(size);
	}

	HANDLE DrawPrimitive::Buffer::getHandle() const
	{
		return m_resource.get();
	}

	void* DrawPrimitive::Buffer::lock(UINT size)
	{
		D3DDDIARG_LOCK lock = {};
		lock.hResource = m_resource.get();
		lock.Range.Offset = m_pos;
		lock.Range.Size = size;
		lock.Flags.RangeValid = 1;

		if (0 == m_pos)
		{
			lock.Flags.Discard = 1;
		}
		else
		{
			lock.Flags.WriteOnly = 1;
			lock.Flags.NoOverwrite = 1;
		}

		m_origVtable.pfnLock(m_device, &lock);
		return lock.pSurfData;
	}

	UINT DrawPrimitive::Buffer::load(const void* src, UINT count, UINT stride)
	{
		if (stride != m_stride)
		{
			m_stride = stride;
			m_pos = roundUpToNearestMultiple(m_pos, stride);
		}

		UINT size = count * m_stride;
		if (m_pos + size > m_size)
		{
			m_pos = 0;
			if (size > m_size)
			{
				if (!resize(roundUpToNearestMultiple(size, m_initialSize)))
				{
					return UINT_MAX;
				}
			}
		}

		auto dst = lock(size);
		if (!dst)
		{
			return UINT_MAX;
		}

		memcpy(dst, src, size);
		unlock();

		UINT pos = m_pos;
		m_pos += size;
		return pos;
	}

	bool DrawPrimitive::Buffer::resize(UINT size)
	{
		if (0 == size)
		{
			m_resource.reset();
			m_size = 0;
			return true;
		}

		D3DDDI_SURFACEINFO surfaceInfo = {};
		surfaceInfo.Width = size;
		surfaceInfo.Height = 1;

		D3DDDIARG_CREATERESOURCE2 cr = {};
		cr.Format = m_format;
		cr.Pool = D3DDDIPOOL_VIDEOMEMORY;
		cr.pSurfList = &surfaceInfo;
		cr.SurfCount = 1;
		cr.Flags.Dynamic = 1;
		cr.Flags.WriteOnly = 1;
		if (D3DDDIFMT_VERTEXDATA == m_format)
		{
			cr.Flags.VertexBuffer = 1;
		}
		else
		{
			cr.Flags.IndexBuffer = 1;
		}

		if (FAILED(m_origVtable.pfnCreateResource2
			? m_origVtable.pfnCreateResource2(m_device, &cr)
			: m_origVtable.pfnCreateResource(m_device, reinterpret_cast<D3DDDIARG_CREATERESOURCE*>(&cr))))
		{
			return false;
		}

		m_resource.reset(cr.hResource);
		m_size = size;
		return true;
	}

	void DrawPrimitive::Buffer::unlock()
	{
		D3DDDIARG_UNLOCK unlock = {};
		unlock.hResource = m_resource.get();
		m_origVtable.pfnUnlock(m_device, &unlock);
	}

	DrawPrimitive::DrawPrimitive(Device& device)
		: m_device(device)
		, m_origVtable(device.getOrigVtable())
		, m_vertexBuffer(m_device, m_origVtable, D3DDDIFMT_VERTEXDATA, 1024 * 1024)
		, m_indexBuffer(m_device, m_origVtable, D3DDDIFMT_INDEX16, 256 * 1024)
		, m_streamSource{}
	{
		if (m_indexBuffer.getHandle())
		{
			D3DDDIARG_SETINDICES si = {};
			si.hIndexBuffer = m_indexBuffer.getHandle();
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

		if (m_streamSource.vertices && m_vertexBuffer.getHandle())
		{
			auto vertexCount = getVertexCount(data.PrimitiveType, data.PrimitiveCount);
			auto vbOffset = m_vertexBuffer.load(firstVertexPtr, vertexCount, m_streamSource.stride);
			if (UINT_MAX == vbOffset)
			{
				return E_OUTOFMEMORY;
			}

			D3DDDIARG_DRAWPRIMITIVE dp = data;
			dp.VStart = vbOffset / m_streamSource.stride;
			return m_origVtable.pfnDrawPrimitive(m_device, &dp, flagBuffer);
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

		if (m_streamSource.vertices && m_vertexBuffer.getHandle())
		{
			data.BaseVertexOffset = m_vertexBuffer.load(firstVertexPtr, data.NumVertices, m_streamSource.stride);
			if (UINT_MAX == data.BaseVertexOffset)
			{
				return E_OUTOFMEMORY;
			}
			data.BaseVertexOffset -= data.MinIndex * m_streamSource.stride;
		}

		if (m_indexBuffer.getHandle() && !flagBuffer)
		{
			D3DDDIARG_DRAWINDEXEDPRIMITIVE dp = {};
			dp.PrimitiveType = data.PrimitiveType;
			dp.BaseVertexIndex = data.BaseVertexOffset / m_streamSource.stride;
			dp.MinIndex = data.MinIndex;
			dp.NumVertices = data.NumVertices;
			dp.StartIndex = m_indexBuffer.load(firstIndexPtr, indexCount, 2) / 2;
			dp.PrimitiveCount = data.PrimitiveCount;
			return m_origVtable.pfnDrawIndexedPrimitive(m_device, &dp);
		}

		return m_origVtable.pfnDrawIndexedPrimitive2(m_device, &data, 2, indices, flagBuffer);
	}

	void DrawPrimitive::removeSysMemVertexBuffer(HANDLE resource)
	{
		m_sysMemVertexBuffers.erase(resource);
	}

	HRESULT DrawPrimitive::setStreamSource(BYTE* vertices, UINT stride, UINT fvf)
	{
		HRESULT result = S_OK;
		if (m_vertexBuffer.getHandle())
		{
			if (!m_streamSource.vertices || stride != m_streamSource.stride)
			{
				D3DDDIARG_SETSTREAMSOURCE ss = {};
				ss.hVertexBuffer = m_vertexBuffer.getHandle();
				ss.Stride = stride;
				result = m_origVtable.pfnSetStreamSource(m_device, &ss);
			}
		}
		else if (vertices != m_streamSource.vertices || stride != m_streamSource.stride)
		{
			D3DDDIARG_SETSTREAMSOURCEUM ss = {};
			ss.Stride = stride;
			result = m_origVtable.pfnSetStreamSourceUm(m_device, &ss, vertices);
		}

		m_streamSource.vertices = vertices;
		m_streamSource.stride = stride;
		m_streamSource.fvf = fvf;
		return result;
	}

	HRESULT DrawPrimitive::setStreamSource(const D3DDDIARG_SETSTREAMSOURCE& data)
	{
		if (0 != data.Stride)
		{
			auto it = m_sysMemVertexBuffers.find(data.hVertexBuffer);
			if (it != m_sysMemVertexBuffers.end())
			{
				return setStreamSource(it->second.vertices + data.Offset, data.Stride, it->second.fvf);
			}
		}

		HRESULT result = m_origVtable.pfnSetStreamSource(m_device, &data);
		if (SUCCEEDED(result))
		{
			m_streamSource = {};
			m_streamSource.stride = data.Stride;
		}
		return result;
	}

	HRESULT DrawPrimitive::setStreamSourceUm(const D3DDDIARG_SETSTREAMSOURCEUM& data, const void* umBuffer)
	{
		return setStreamSource(static_cast<BYTE*>(const_cast<void*>(umBuffer)), data.Stride, 0);
	}
}
