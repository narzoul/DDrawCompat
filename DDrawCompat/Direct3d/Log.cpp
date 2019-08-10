#include <Direct3d/Log.h>

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

std::ostream& operator<<(std::ostream& os, const D3DVERTEXBUFFERDESC& data)
{
	return Compat::LogStruct(os)
		<< Compat::hex(data.dwCaps)
		<< Compat::hex(data.dwFVF)
		<< data.dwNumVertices;
}
