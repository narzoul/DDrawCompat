#include <Direct3d/Log.h>

std::ostream& operator<<(std::ostream& os, const D3DCOLORVALUE& data)
{
	return Compat::LogStruct(os)
		<< data.r
		<< data.g
		<< data.b
		<< data.a;
}

std::ostream& operator<<(std::ostream& os, const D3DDP_PTRSTRIDE& data)
{
	return Compat::LogStruct(os)
		<< data.lpvData
		<< data.dwStride;
}

std::ostream& operator<<(std::ostream& os, const D3DDRAWPRIMITIVESTRIDEDDATA& data)
{
	return Compat::LogStruct(os)
		<< data.position
		<< data.normal
		<< data.diffuse
		<< data.specular
		<< Compat::array(data.textureCoords, D3DDP_MAXTEXCOORD);
}

std::ostream& operator<<(std::ostream& os, const D3DEXECUTEBUFFERDESC& data)
{
	return Compat::LogStruct(os)
		<< Compat::hex(data.dwFlags)
		<< Compat::hex(data.dwCaps)
		<< data.dwBufferSize
		<< data.lpData;
}

std::ostream& operator<<(std::ostream& os, const D3DEXECUTEDATA& data)
{
	return Compat::LogStruct(os)
		<< data.dwVertexOffset
		<< data.dwVertexCount
		<< data.dwInstructionOffset
		<< data.dwInstructionLength
		<< data.dwHVertexOffset
		<< data.dsStatus;
}

std::ostream& operator<<(std::ostream& os, const D3DLIGHT& data)
{
	D3DLIGHT2 light = {};
	reinterpret_cast<D3DLIGHT&>(light) = data;
	return os << light;
}

std::ostream& operator<<(std::ostream& os, const D3DLIGHT2& data)
{
	return Compat::LogStruct(os)
		<< data.dltType
		<< data.dcvColor
		<< data.dvPosition
		<< data.dvDirection
		<< data.dvRange
		<< data.dvFalloff
		<< data.dvAttenuation0
		<< data.dvAttenuation1
		<< data.dvAttenuation2
		<< data.dvTheta
		<< data.dvPhi
		<< Compat::hex(data.dwFlags);
}

std::ostream& operator<<(std::ostream& os, const D3DLIGHT7& data)
{
	return Compat::LogStruct(os)
		<< data.dltType
		<< data.dcvDiffuse
		<< data.dcvSpecular
		<< data.dcvAmbient
		<< data.dvPosition
		<< data.dvDirection
		<< data.dvRange
		<< data.dvFalloff
		<< data.dvAttenuation0
		<< data.dvAttenuation1
		<< data.dvAttenuation2
		<< data.dvTheta
		<< data.dvPhi;
}

std::ostream& operator<<(std::ostream& os, const D3DMATERIAL& data)
{
	return Compat::LogStruct(os)
		<< data.diffuse
		<< data.ambient
		<< data.specular
		<< data.emissive
		<< data.power
		<< Compat::hex(data.hTexture)
		<< data.dwRampSize;
}

std::ostream& operator<<(std::ostream& os, const D3DMATERIAL7& data)
{
	D3DMATERIAL material = {};
	reinterpret_cast<D3DMATERIAL7&>(material) = data;
	return os << material;
}

std::ostream& operator<<(std::ostream& os, const D3DRECT& data)
{
	return Compat::LogStruct(os)
		<< data.x1
		<< data.y1
		<< data.x2
		<< data.y2;
}

std::ostream& operator<<(std::ostream& os, const D3DSTATUS& data)
{
	return Compat::LogStruct(os)
		<< Compat::hex(data.dwFlags)
		<< Compat::hex(data.dwStatus)
		<< data.drExtent;
}

std::ostream& operator<<(std::ostream& os, const D3DVERTEXBUFFERDESC& data)
{
	return Compat::LogStruct(os)
		<< Compat::hex(data.dwCaps)
		<< Compat::hex(data.dwFVF)
		<< data.dwNumVertices;
}
