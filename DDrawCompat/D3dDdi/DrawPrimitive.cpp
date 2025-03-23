#include <algorithm>

#include <Common/Log.h>
#include <Config/Settings/SpriteDetection.h>
#include <Config/Settings/SpriteTexCoord.h>
#include <D3dDdi/DrawPrimitive.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/Resource.h>

namespace
{
	const UINT INDEX_BUFFER_SIZE = D3DMAXNUMPRIMITIVES * 3 * sizeof(UINT16);
	const UINT VERTEX_BUFFER_SIZE = 1024 * 1024;

	enum VertexFixupFlags
	{
		VF_XY       = 1 << 0,
		VF_Z        = 1 << 1,
		VF_RHW      = 1 << 2,
		VF_TEXCOORD = 1 << 3
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

	void updateMax(UINT& max, UINT value)
	{
		if (value > max)
		{
			max = value;
		}
	}

	void updateMin(UINT& min, UINT value)
	{
		if (value < min)
		{
			min = value;
		}
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
		, m_batched{}
		, m_vertexFixupFlags(0)
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

	void DrawPrimitive::addSysMemVertexBuffer(HANDLE resource, BYTE* vertices)
	{
		m_sysMemVertexBuffers[resource] = vertices;
	}

	void DrawPrimitive::appendIndexedVertices(const UINT16* indices, UINT count,
		INT baseVertexIndex, UINT minIndex, UINT maxIndex)
	{
		rebaseIndices();
		appendIndexedVerticesWithoutRebase(indices, count, baseVertexIndex, minIndex, maxIndex);
	}

	void DrawPrimitive::appendIndexedVerticesWithoutRebase(const UINT16* indices, UINT count,
		INT baseVertexIndex, UINT minIndex, UINT maxIndex)
	{
		UINT vertexCount = maxIndex - minIndex + 1;
		if (vertexCount <= count)
		{
			INT delta = getBatchedVertexCount() - minIndex;
			for (UINT i = 0; i < count; ++i)
			{
				m_batched.indices.push_back(static_cast<UINT16>(indices[i] + delta));
			}
			appendVertices(baseVertexIndex + minIndex, vertexCount);
			return;
		}

		static UINT16 indexMap[D3DMAXNUMVERTICES] = {};
		static BYTE indexCycles[D3DMAXNUMVERTICES] = {};
		static BYTE currentCycle = 0;
		static UINT maxVertexCount = 0;

		++currentCycle;
		if (0 == currentCycle)
		{
			memset(indexCycles, 0, maxVertexCount);
			maxVertexCount = vertexCount;
			++currentCycle;
		}
		else
		{
			updateMax(maxVertexCount, vertexCount);
		}

		UINT16 newIndex = static_cast<UINT16>(getBatchedVertexCount());
		for (UINT i = 0; i < count; ++i)
		{
			const UINT16 zeroBasedIndex = static_cast<UINT16>(indices[i] - minIndex);
			if (currentCycle != indexCycles[zeroBasedIndex])
			{
				appendVertices(baseVertexIndex + indices[i], 1);
				indexMap[zeroBasedIndex] = newIndex;
				indexCycles[zeroBasedIndex] = currentCycle;
				m_batched.indices.push_back(newIndex);
				++newIndex;
			}
			else
			{
				m_batched.indices.push_back(indexMap[zeroBasedIndex]);
			}
		}
	}

	void DrawPrimitive::appendIndexRange(UINT base, UINT count)
	{
		rebaseIndices();
		appendIndexRangeWithoutRebase(base, count);
	}

	void DrawPrimitive::appendIndexRangeWithoutRebase(UINT base, UINT count)
	{
		for (UINT i = base; i < base + count; ++i)
		{
			m_batched.indices.push_back(static_cast<UINT16>(i));
		}
		updateMin(m_batched.minIndex, base);
		updateMax(m_batched.maxIndex, base + count - 1);
	}

	void DrawPrimitive::appendIndices(const UINT16* indices, UINT count,
		INT baseVertexIndex, UINT minIndex, UINT maxIndex)
	{
		rebaseIndices();
		for (UINT i = 0; i < count; ++i)
		{
			m_batched.indices.push_back(static_cast<UINT16>(baseVertexIndex + indices[i]));
		}
		updateMin(m_batched.minIndex, baseVertexIndex + minIndex);
		updateMax(m_batched.maxIndex, baseVertexIndex + maxIndex);
	}

	void DrawPrimitive::appendIndicesAndVertices(const UINT16* indices, UINT count,
		INT baseVertexIndex, UINT minIndex, UINT maxIndex)
	{
		if (m_streamSource.vertices)
		{
			if (indices)
			{
				appendIndexedVertices(indices, count, baseVertexIndex, minIndex, maxIndex);
			}
			else
			{
				if (!m_batched.indices.empty())
				{
					appendIndexRange(getBatchedVertexCount(), count);
				}
				appendVertices(baseVertexIndex + minIndex, count);
			}
		}
		else if (indices)
		{
			appendIndices(indices, count, baseVertexIndex, minIndex, maxIndex);
		}
		else
		{
			appendIndexRange(baseVertexIndex, count);
		}
	}

	void DrawPrimitive::appendLineOrTriangleList(INT baseVertexIndex, UINT primitiveCount, UINT vpp,
		const UINT16* indices, UINT minIndex, UINT maxIndex)
	{
		if (m_streamSource.vertices ||
			indices ||
			!m_batched.indices.empty() ||
			m_batched.baseVertexIndex + static_cast<INT>(m_batched.primitiveCount * vpp) != baseVertexIndex)
		{
			appendIndicesAndVertices(indices, primitiveCount * vpp, baseVertexIndex, minIndex, maxIndex);
		}
	}

	bool DrawPrimitive::appendPrimitives(D3DPRIMITIVETYPE primitiveType, INT baseVertexIndex, UINT primitiveCount,
		const UINT16* indices, UINT minIndex, UINT maxIndex)
	{
		if ((m_batched.primitiveCount + primitiveCount) * 3 > D3DMAXNUMVERTICES)
		{
			return false;
		}

		switch (primitiveType)
		{
		case D3DPT_POINTLIST:
			if (D3DPT_POINTLIST != m_batched.primitiveType ||
				!m_streamSource.vertices &&
				m_batched.baseVertexIndex + static_cast<INT>(m_batched.primitiveCount) != baseVertexIndex)
			{
				return false;
			}
			if (m_streamSource.vertices)
			{
				appendVertices(baseVertexIndex, primitiveCount);
			}
			break;

		case D3DPT_LINESTRIP:
			return false;

		case D3DPT_LINELIST:
			if (D3DPT_LINELIST != m_batched.primitiveType)
			{
				return false;
			}
			appendLineOrTriangleList(baseVertexIndex, primitiveCount, 2, indices, minIndex, maxIndex);
			break;

		case D3DPT_TRIANGLELIST:
			if (m_batched.primitiveType < D3DPT_TRIANGLELIST)
			{
				return false;
			}
			convertToTriangleList();
			appendLineOrTriangleList(baseVertexIndex, primitiveCount, 3, indices, minIndex, maxIndex);
			break;

		case D3DPT_TRIANGLESTRIP:
			if (m_batched.primitiveType < D3DPT_TRIANGLELIST)
			{
				return false;
			}
			appendTriangleStrip(baseVertexIndex, primitiveCount, indices, minIndex, maxIndex);
			break;

		case D3DPT_TRIANGLEFAN:
			if (m_batched.primitiveType < D3DPT_TRIANGLELIST)
			{
				return false;
			}
			appendTriangleFan(baseVertexIndex, primitiveCount, indices, minIndex, maxIndex);
			break;
		}

		m_batched.primitiveCount += primitiveCount;
		return true;
	}

	void DrawPrimitive::appendTriangleFan(INT baseVertexIndex, UINT primitiveCount,
		const UINT16* indices, UINT minIndex, UINT maxIndex)
	{
		convertToTriangleList();
		rebaseIndices();
		appendIndicesAndVertices(indices, primitiveCount + 2, baseVertexIndex, minIndex, maxIndex);
		convertIndexedTriangleFanToList(m_batched.primitiveCount, primitiveCount);
	}

	void DrawPrimitive::appendTriangleStrip(INT baseVertexIndex, UINT primitiveCount,
		const UINT16* indices, UINT minIndex, UINT maxIndex)
	{
		if (D3DPT_TRIANGLESTRIP != m_batched.primitiveType)
		{
			convertToTriangleList();
			rebaseIndices();
			appendIndicesAndVertices(indices, primitiveCount + 2, baseVertexIndex, minIndex, maxIndex);
			convertIndexedTriangleStripToList(m_batched.primitiveCount, primitiveCount);
			return;
		}

		if (!m_streamSource.vertices || !m_batched.indices.empty())
		{
			rebaseIndices();
		}

		for (UINT i = 1 + m_batched.primitiveCount % 2; i != 0; --i)
		{
			repeatLastBatchedVertex();
			m_batched.primitiveCount++;
		}

		if (m_batched.indices.empty())
		{
			appendVertices(baseVertexIndex, 1);
		}
		else
		{
			if (m_streamSource.vertices)
			{
				m_batched.indices.push_back(static_cast<UINT16>(getBatchedVertexCount()));
			}
			else if (indices)
			{
				m_batched.indices.push_back(static_cast<UINT16>(baseVertexIndex + indices[0]));
			}
			else
			{
				m_batched.indices.push_back(static_cast<UINT16>(baseVertexIndex));
			}
		}
		m_batched.primitiveCount += 3;

		appendIndicesAndVertices(indices, primitiveCount + 2, baseVertexIndex, minIndex, maxIndex);
	}

	void DrawPrimitive::appendVertices(UINT base, UINT count)
	{
		auto vertices = m_streamSource.vertices + base * m_streamSource.stride;
		m_batched.vertices.insert(m_batched.vertices.end(), vertices, vertices + count * m_streamSource.stride);

		if (Config::logLevel.get() >= Config::Settings::LogLevel::TRACE)
		{
			Compat::Log log(Config::Settings::LogLevel::TRACE);
			log << '[';
			auto vPos = &m_batched.vertices[m_batched.vertices.size() - count * m_streamSource.stride];
			for (unsigned i = 0; i < count; ++i)
			{
				auto v = reinterpret_cast<D3DTLVERTEX*>(vPos);
				if (0 != i)
				{
					log << ',';
				}
				log << '{' << v->sx << ',' << v->sy << ',' << v->sz << ',' << v->rhw << '}';
				vPos += m_streamSource.stride;
			}
			log << ']';
		}

		if (0 == m_vertexFixupFlags)
		{
			return;
		}

		auto& vertexFixupData = m_device.getState().getVertexFixupData();
		auto firstVertex = &m_batched.vertices[m_batched.vertices.size() - count * m_streamSource.stride];

		if (m_vertexFixupFlags & VF_XY)
		{
			auto vPos = firstVertex;
			for (unsigned i = 0; i < count; ++i)
			{
				auto v = reinterpret_cast<D3DTLVERTEX*>(vPos);
				v->sx += vertexFixupData.offset[0];
				v->sy += vertexFixupData.offset[1];
				v->sx *= vertexFixupData.multiplier[0];
				v->sy *= vertexFixupData.multiplier[1];
				vPos += m_streamSource.stride;
			}
		}

		if (m_vertexFixupFlags & VF_Z)
		{
			auto zPos = reinterpret_cast<BYTE*>(&reinterpret_cast<D3DTLVERTEX*>(firstVertex)->sz);
			for (unsigned i = 0; i < count; ++i)
			{
				auto& z = *reinterpret_cast<float*>(zPos);
				if (z > 1)
				{
					z = 1;
				}
				else if (z < 0 || isnan(z))
				{
					z = 0;
				}
				zPos += m_streamSource.stride;
			}
		}

		if (m_vertexFixupFlags & VF_RHW)
		{
			auto rhwPos = reinterpret_cast<BYTE*>(&reinterpret_cast<D3DTLVERTEX*>(firstVertex)->rhw);
			for (unsigned i = 0; i < count; ++i)
			{
				*reinterpret_cast<float*>(rhwPos) = 1;
				rhwPos += m_streamSource.stride;
			}
		}

		if (m_vertexFixupFlags & VF_TEXCOORD)
		{
			auto tcPos = firstVertex + m_device.getState().getVertexDecl().texCoordOffset[0];
			for (unsigned i = 0; i < count; ++i)
			{
				float* tc = reinterpret_cast<float*>(tcPos);
				tc[0] *= vertexFixupData.texCoordAdj[0];
				tc[1] *= vertexFixupData.texCoordAdj[1];
				tc[0] += vertexFixupData.texCoordAdj[2];
				tc[1] += vertexFixupData.texCoordAdj[3];
				tc[0] = roundf(tc[0]);
				tc[1] = roundf(tc[1]);
				tc[0] /= vertexFixupData.texCoordAdj[0];
				tc[1] /= vertexFixupData.texCoordAdj[1];
				tcPos += m_streamSource.stride;
			}
		}
	}

	void DrawPrimitive::clearBatchedPrimitives()
	{
		m_batched.primitiveCount = 0;
		m_batched.vertices.clear();
		m_batched.indices.clear();
	}

	void DrawPrimitive::convertIndexedTriangleFanToList(UINT startPrimitive, UINT primitiveCount)
	{
		const UINT totalPrimitiveCount = startPrimitive + primitiveCount;
		m_batched.indices.resize(totalPrimitiveCount * 3);

		INT startIndexPos = startPrimitive * 3;
		INT oldIndexPos = startIndexPos + primitiveCount - 1;
		INT newIndexPos = (totalPrimitiveCount - 1) * 3;
		const UINT16 startIndex = m_batched.indices[startIndexPos];

		while (newIndexPos > startIndexPos)
		{
			m_batched.indices[newIndexPos + 2] = startIndex;
			m_batched.indices[newIndexPos + 1] = m_batched.indices[oldIndexPos + 2];
			m_batched.indices[newIndexPos] = m_batched.indices[oldIndexPos + 1];
			newIndexPos -= 3;
			oldIndexPos--;
		}

		m_batched.indices[newIndexPos] = m_batched.indices[oldIndexPos + 1];
		m_batched.indices[newIndexPos + 1] = m_batched.indices[oldIndexPos + 2];
		m_batched.indices[newIndexPos + 2] = startIndex;
	}

	void DrawPrimitive::convertIndexedTriangleStripToList(UINT startPrimitive, UINT primitiveCount)
	{
		const UINT totalPrimitiveCount = startPrimitive + primitiveCount;
		m_batched.indices.resize(totalPrimitiveCount * 3);

		INT oldIndexPos = startPrimitive * 3 + primitiveCount - 2;
		INT newIndexPos = (totalPrimitiveCount - 2) * 3;

		if (0 != primitiveCount % 2)
		{
			m_batched.indices[newIndexPos + 5] = m_batched.indices[oldIndexPos + 3];
			m_batched.indices[newIndexPos + 4] = m_batched.indices[oldIndexPos + 2];
			m_batched.indices[newIndexPos + 3] = m_batched.indices[oldIndexPos + 1];
			newIndexPos -= 3;
			oldIndexPos--;
		}

		while (newIndexPos >= oldIndexPos)
		{
			m_batched.indices[newIndexPos + 5] = m_batched.indices[oldIndexPos + 2];
			m_batched.indices[newIndexPos + 4] = m_batched.indices[oldIndexPos + 3];
			m_batched.indices[newIndexPos + 3] = m_batched.indices[oldIndexPos + 1];
			m_batched.indices[newIndexPos + 2] = m_batched.indices[oldIndexPos + 2];
			m_batched.indices[newIndexPos + 1] = m_batched.indices[oldIndexPos + 1];
			m_batched.indices[newIndexPos] = m_batched.indices[oldIndexPos];
			newIndexPos -= 6;
			oldIndexPos -= 2;
		}
	}

	void DrawPrimitive::convertToTriangleList()
	{
		const bool alreadyIndexed = !m_batched.indices.empty();

		switch (m_batched.primitiveType)
		{
		case D3DPT_TRIANGLELIST:
			return;

		case D3DPT_TRIANGLESTRIP:
			if (alreadyIndexed)
			{
				rebaseIndices();
				convertIndexedTriangleStripToList(0, m_batched.primitiveCount);
			}
			else
			{
				const UINT baseVertexIndex = static_cast<UINT>(m_batched.baseVertexIndex);
				UINT i = baseVertexIndex;
				for (; i < baseVertexIndex + m_batched.primitiveCount - 1; i += 2)
				{
					m_batched.indices.push_back(static_cast<UINT16>(i));
					m_batched.indices.push_back(static_cast<UINT16>(i + 1));
					m_batched.indices.push_back(static_cast<UINT16>(i + 2));
					m_batched.indices.push_back(static_cast<UINT16>(i + 1));
					m_batched.indices.push_back(static_cast<UINT16>(i + 3));
					m_batched.indices.push_back(static_cast<UINT16>(i + 2));
				}
				if (i < baseVertexIndex + m_batched.primitiveCount)
				{
					m_batched.indices.push_back(static_cast<UINT16>(i));
					m_batched.indices.push_back(static_cast<UINT16>(i + 1));
					m_batched.indices.push_back(static_cast<UINT16>(i + 2));
				}
			}
			break;

		case D3DPT_TRIANGLEFAN:
			if (alreadyIndexed)
			{
				rebaseIndices();
				convertIndexedTriangleFanToList(0, m_batched.primitiveCount);
			}
			else
			{
				for (UINT i = m_batched.baseVertexIndex; i < m_batched.baseVertexIndex + m_batched.primitiveCount; ++i)
				{
					m_batched.indices.push_back(static_cast<UINT16>(i + 1));
					m_batched.indices.push_back(static_cast<UINT16>(i + 2));
					m_batched.indices.push_back(static_cast<UINT16>(m_batched.baseVertexIndex));
				}
			}
			break;
		}

		m_batched.primitiveType = D3DPT_TRIANGLELIST;
		if (!alreadyIndexed)
		{
			m_batched.minIndex = m_batched.baseVertexIndex;
			m_batched.maxIndex = m_batched.baseVertexIndex + m_batched.primitiveCount + 1;
			m_batched.baseVertexIndex = 0;
		}
	}

	HRESULT DrawPrimitive::draw(D3DDDIARG_DRAWPRIMITIVE data, const UINT* flagBuffer)
	{
		auto& state = m_device.getState();
		auto vertexCount = getVertexCount(data.PrimitiveType, data.PrimitiveCount);
		m_vertexFixupFlags = 0;
		if (!state.isLocked())
		{
			if (0 == m_batched.primitiveCount)
			{
				m_device.prepareForGpuWrite();
			}
			state.updateStreamSource();
			if (m_streamSource.vertices && data.PrimitiveType >= D3DPT_TRIANGLELIST)
			{
				bool spriteMode = isSprite(data.VStart, 0, 1, 2);
				state.setSpriteMode(spriteMode);
				if (spriteMode)
				{
					setTextureClampMode(data.VStart, nullptr, vertexCount);
				}
			}
			else
			{
				state.setSpriteMode(false);
			}
			state.flush();
			setVertexFixupFlags(data.VStart, 0);
		}

		if (0 == m_batched.primitiveCount || flagBuffer ||
			!appendPrimitives(data.PrimitiveType, data.VStart, data.PrimitiveCount, nullptr, 0, 0))
		{
			flushPrimitives();
			if (m_streamSource.vertices)
			{
				appendVertices(data.VStart, vertexCount);
				m_batched.baseVertexIndex = 0;
			}
			else
			{
				m_batched.baseVertexIndex = data.VStart;
				m_batched.minIndex = D3DMAXNUMVERTICES;
				m_batched.maxIndex = 0;
			}
			m_batched.primitiveType = data.PrimitiveType;
			m_batched.primitiveCount = data.PrimitiveCount;

			if (flagBuffer)
			{
				flushPrimitives(flagBuffer);
			}
		}

		return S_OK;
	}

	HRESULT DrawPrimitive::drawIndexed(
		D3DDDIARG_DRAWINDEXEDPRIMITIVE2 data, const UINT16* indices, const UINT* flagBuffer)
	{
		auto& state = m_device.getState();
		if (!state.isLocked())
		{
			if (0 == m_batched.primitiveCount)
			{
				m_device.prepareForGpuWrite();
			}
			state.updateStreamSource();
		}

		auto indexCount = getVertexCount(data.PrimitiveType, data.PrimitiveCount);
		auto vStart = data.BaseVertexOffset / static_cast<INT>(m_streamSource.stride);
		m_vertexFixupFlags = 0;
		if (!state.isLocked())
		{
			if (m_streamSource.vertices && data.PrimitiveType >= D3DPT_TRIANGLELIST)
			{
				bool spriteMode = isSprite(vStart, indices[0], indices[1], indices[2]);
				state.setSpriteMode(spriteMode);
				if (spriteMode)
				{
					setTextureClampMode(vStart, indices, indexCount);
				}
			}
			else
			{
				state.setSpriteMode(false);
			}
			state.flush();
			setVertexFixupFlags(vStart, indices[0]);
		}

		auto [min, max] = std::minmax_element(indices, indices + indexCount);
		data.MinIndex = *min;
		data.NumVertices = *max - *min + 1;

		if (0 == m_batched.primitiveCount || flagBuffer ||
			!appendPrimitives(data.PrimitiveType, vStart, data.PrimitiveCount, indices, *min, *max))
		{
			flushPrimitives();
			m_batched.baseVertexIndex = vStart;
			if (m_streamSource.vertices)
			{
				appendIndexedVerticesWithoutRebase(indices, indexCount, m_batched.baseVertexIndex, *min, *max);
				m_batched.baseVertexIndex = 0;
			}
			else
			{
				m_batched.indices.assign(indices, indices + indexCount);
				m_batched.minIndex = *min;
				m_batched.maxIndex = *max;
			}
			m_batched.primitiveType = data.PrimitiveType;
			m_batched.primitiveCount = data.PrimitiveCount;

			if (flagBuffer)
			{
				flushPrimitives(flagBuffer);
			}
		}

		return S_OK;
	}

	HRESULT DrawPrimitive::flush(const UINT* flagBuffer)
	{
		D3DDDIARG_DRAWPRIMITIVE data = {};
		data.PrimitiveType = m_batched.primitiveType;
		data.VStart = m_batched.baseVertexIndex;
		data.PrimitiveCount = m_batched.primitiveCount;

		if (m_streamSource.vertices)
		{
			data.VStart = loadVertices(getBatchedVertexCount());
		}

		clearBatchedPrimitives();
		return m_origVtable.pfnDrawPrimitive(m_device, &data, flagBuffer);
	}

	HRESULT DrawPrimitive::flushIndexed(const UINT* flagBuffer)
	{
		D3DDDIARG_DRAWINDEXEDPRIMITIVE2 data = {};
		data.PrimitiveType = m_batched.primitiveType;
		data.BaseVertexOffset = m_batched.baseVertexIndex * static_cast<INT>(m_streamSource.stride);
		if (m_streamSource.vertices)
		{
			data.MinIndex = -m_batched.baseVertexIndex;
			data.NumVertices = getBatchedVertexCount();
		}
		else
		{
			data.MinIndex = m_batched.minIndex;
			data.NumVertices = m_batched.maxIndex - m_batched.minIndex + 1;
		}
		data.PrimitiveCount = m_batched.primitiveCount;

		if (m_streamSource.vertices)
		{
			INT baseVertexIndex = loadVertices(data.NumVertices) - data.MinIndex;
			data.BaseVertexOffset = baseVertexIndex * static_cast<INT>(m_streamSource.stride);
		}

		INT startIndex = -1;
		if ((!m_streamSource.vertices || m_vertexBuffer) && m_indexBuffer && !flagBuffer)
		{
			startIndex = loadIndices(m_batched.indices.data(), m_batched.indices.size());
		}

		HRESULT result = S_OK;
		if (startIndex >= 0)
		{
			D3DDDIARG_DRAWINDEXEDPRIMITIVE dp = {};
			dp.PrimitiveType = data.PrimitiveType;
			dp.BaseVertexIndex = data.BaseVertexOffset / static_cast<INT>(m_streamSource.stride);
			dp.MinIndex = data.MinIndex;
			dp.NumVertices = data.NumVertices;
			dp.StartIndex = startIndex;
			dp.PrimitiveCount = data.PrimitiveCount;
			result = m_origVtable.pfnDrawIndexedPrimitive(m_device, &dp);
		}
		else
		{
			result = m_origVtable.pfnDrawIndexedPrimitive2(m_device, &data, 2, m_batched.indices.data(), flagBuffer);
		}

		clearBatchedPrimitives();
		return result;
	}

	HRESULT DrawPrimitive::flushPrimitives(const UINT* flagBuffer)
	{
		if (0 == m_batched.primitiveCount)
		{
			return S_OK;
		}

		LOG_DEBUG << "Flushing " << m_batched.primitiveCount << " primitives of type " << m_batched.primitiveType;
		return m_batched.indices.empty() ? flush(flagBuffer) : flushIndexed(flagBuffer);
	}

	UINT DrawPrimitive::getBatchedVertexCount() const
	{
		return m_batched.vertices.size() / m_streamSource.stride;
	}

	bool DrawPrimitive::isSprite(INT baseVertexIndex, UINT16 index0, UINT16 index1, UINT16 index2)
	{
		auto spriteDetection = Config::spriteDetection.get();
		if (Config::Settings::SpriteDetection::OFF == spriteDetection ||
			Config::Settings::SpriteDetection::POINT == spriteDetection && (
				D3DTEXF_POINT != m_device.getState().getAppState().textureStageState[0][D3DDDITSS_MAGFILTER] ||
				D3DTEXF_POINT != m_device.getState().getAppState().textureStageState[0][D3DDDITSS_MINFILTER]))
		{
			return false;
		}

		auto v = m_streamSource.vertices + baseVertexIndex * m_streamSource.stride;
		auto v0 = reinterpret_cast<const D3DTLVERTEX*>(v + index0 * m_streamSource.stride);
		if (Config::Settings::SpriteDetection::ZMAX == spriteDetection &&
			v0->sz > static_cast<float>(Config::spriteDetection.getParam()) / 100)
		{
			return false;
		}

		auto v1 = reinterpret_cast<const D3DTLVERTEX*>(v + index1 * m_streamSource.stride);
		auto v2 = reinterpret_cast<const D3DTLVERTEX*>(v + index2 * m_streamSource.stride);
		return v0->sz == v1->sz && v0->sz == v2->sz;
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

	INT DrawPrimitive::loadVertices(UINT count)
	{
		auto vertices = m_batched.vertices.data();
		if (m_vertexBuffer)
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

			m_vertexBuffer.resize(0);
			m_indexBuffer.resize(0);
		}

		D3DDDIARG_SETSTREAMSOURCEUM ss = {};
		ss.Stride = m_streamSource.stride;
		m_origVtable.pfnSetStreamSourceUm(m_device, &ss, vertices);
		return 0;
	}

	void DrawPrimitive::rebaseIndices()
	{
		if (0 != m_batched.baseVertexIndex || m_batched.indices.empty())
		{
			if (m_batched.indices.empty())
			{
				auto vertexCount = getVertexCount(m_batched.primitiveType, m_batched.primitiveCount);
				appendIndexRangeWithoutRebase(m_batched.baseVertexIndex, vertexCount);
			}
			else
			{
				for (auto& index : m_batched.indices)
				{
					index = static_cast<UINT16>(m_batched.baseVertexIndex + index);
				}
				m_batched.minIndex += m_batched.baseVertexIndex;
				m_batched.maxIndex += m_batched.baseVertexIndex;
			}
			m_batched.baseVertexIndex = 0;
		}
	}

	void DrawPrimitive::repeatLastBatchedVertex()
	{
		if (m_batched.indices.empty())
		{
			m_batched.vertices.reserve(m_batched.vertices.size() + m_streamSource.stride);
			m_batched.vertices.insert(m_batched.vertices.end(),
				m_batched.vertices.end() - m_streamSource.stride, m_batched.vertices.end());
		}
		else
		{
			m_batched.indices.push_back(m_batched.indices.back());
		}
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
			return setSysMemStreamSource(it->second, data.Stride);
		}

		flushPrimitives();
		HRESULT result = m_origVtable.pfnSetStreamSource(m_device, &data);
		if (SUCCEEDED(result))
		{
			m_streamSource = { nullptr, data.Stride };
		}
		return result;
	}

	HRESULT DrawPrimitive::setStreamSourceUm(const D3DDDIARG_SETSTREAMSOURCEUM& data, const void* umBuffer)
	{
		return setSysMemStreamSource(static_cast<const BYTE*>(umBuffer), data.Stride);
	}

	HRESULT DrawPrimitive::setSysMemStreamSource(const BYTE* vertices, UINT stride)
	{
		HRESULT result = S_OK;
		if (!m_streamSource.vertices || stride != m_streamSource.stride)
		{
			flushPrimitives();
			if (m_vertexBuffer)
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

		if (SUCCEEDED(result))
		{
			m_streamSource = { vertices, stride };
		}
		return result;
	}

	void DrawPrimitive::setTextureClampMode(INT baseVertexIndex, const UINT16* indices, UINT count)
	{
		if (Config::Settings::SpriteTexCoord::CLAMP != Config::spriteTexCoord.get())
		{
			return;
		}

		auto& state = m_device.getState();
		auto& appState = state.getAppState();
		auto& decl = state.getVertexDecl();
		auto textureStageCount = state.getTextureStageCount();
		auto vertices = m_streamSource.vertices + baseVertexIndex * m_streamSource.stride;

		for (UINT stage = 0; stage < textureStageCount; ++stage)
		{
			const UINT D3DDECLTYPE_FLOAT2 = 1;
			if (!appState.textures[stage] ||
				D3DDECLTYPE_FLOAT2 != decl.texCoordType[stage] ||
				D3DTADDRESS_CLAMP == appState.textureStageState[stage][D3DDDITSS_ADDRESSU] &&
				D3DTADDRESS_CLAMP == appState.textureStageState[stage][D3DDDITSS_ADDRESSV])
			{
				continue;
			}
			
			auto resource = state.getTextureResource(stage);
			if (!resource || !resource->isClampable())
			{
				continue;
			}

			const float texelWidth = 1 / static_cast<float>(resource->getFixedDesc().pSurfList[0].Width);
			const float texelHeight = 1 / static_cast<float>(resource->getFixedDesc().pSurfList[0].Height);
			const float minU = -texelWidth;
			const float maxU = 1 + texelWidth;
			const float minV = -texelHeight;
			const float maxV = 1 + texelHeight;

			for (UINT i = 0; i < count; ++i)
			{
				auto vertex = vertices + (indices ? indices[i] : i) * m_streamSource.stride;
				const float* texCoord = reinterpret_cast<const float*>(vertex + decl.texCoordOffset[stage]);
				if (texCoord[0] < minU || texCoord[0] > maxU || texCoord[1] < minV || texCoord[1] > maxV)
				{
					state.disableTextureClamp(stage);
					break;
				}
			}
		}
	}

	void DrawPrimitive::setVertexFixupFlags(INT baseVertexIndex, UINT16 index)
	{
		auto& state = m_device.getState();
		if (!state.getVertexDecl().isTransformed ||
			state.getCurrentState().vertexShaderFunc != state.getAppState().vertexShaderFunc)
		{
			return;
		}

		auto& vertexFixupData = state.getVertexFixupData();
		if (0 != vertexFixupData.offset[0] ||
			0 != vertexFixupData.offset[1] ||
			1 != vertexFixupData.multiplier[0] ||
			1 != vertexFixupData.multiplier[1])
		{
			m_vertexFixupFlags |= VF_XY;
		}

		if (0 == state.getTextureStageCount() && !state.getAppState().renderState[D3DDDIRS_FOGENABLE])
		{
			m_vertexFixupFlags |= VF_Z | VF_RHW;
		}
		else
		{
			auto vertex = reinterpret_cast<const D3DTLVERTEX*>(
				m_streamSource.vertices + (baseVertexIndex + index) * m_streamSource.stride);
			if (vertex->sz > 1 || vertex->sz < 0 || isnan(vertex->sz))
			{
				m_vertexFixupFlags |= VF_Z;
			}
		}

		const UINT D3DDECLTYPE_FLOAT2 = 1;
		if (state.getSpriteMode() &&
			Config::Settings::SpriteTexCoord::ROUND == Config::spriteTexCoord.get() &&
			0 != Config::spriteTexCoord.getParam() &&
			D3DDECLTYPE_FLOAT2 == state.getVertexDecl().texCoordType[0])
		{
			m_vertexFixupFlags |= VF_TEXCOORD;
		}
	}
}
