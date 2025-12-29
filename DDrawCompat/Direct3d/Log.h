#pragma once

#include <d3d.h>

#include <Common/Log.h>

std::ostream& operator<<(std::ostream& os, const D3DBRANCH& data);
std::ostream& operator<<(std::ostream& os, const D3DCOLORVALUE& data);
std::ostream& operator<<(std::ostream& os, const D3DDP_PTRSTRIDE& data);
std::ostream& operator<<(std::ostream& os, const D3DDRAWPRIMITIVESTRIDEDDATA& data);
std::ostream& operator<<(std::ostream& os, const D3DEXECUTEBUFFERDESC& data);
std::ostream& operator<<(std::ostream& os, const D3DEXECUTEDATA& data);
std::ostream& operator<<(std::ostream& os, const D3DINSTRUCTION& data);
std::ostream& operator<<(std::ostream& os, const D3DLIGHT& data);
std::ostream& operator<<(std::ostream& os, const D3DLIGHT2& data);
std::ostream& operator<<(std::ostream& os, const D3DLIGHT7& data);
std::ostream& operator<<(std::ostream& os, D3DLIGHTSTATETYPE data);
std::ostream& operator<<(std::ostream& os, const D3DLINE& data);
std::ostream& operator<<(std::ostream& os, const D3DLVERTEX& data);
std::ostream& operator<<(std::ostream& os, const D3DMATERIAL& data);
std::ostream& operator<<(std::ostream& os, const D3DMATERIAL7& data);
std::ostream& operator<<(std::ostream& os, const D3DMATRIX& data);
std::ostream& operator<<(std::ostream& os, const D3DMATRIXLOAD& data);
std::ostream& operator<<(std::ostream& os, const D3DMATRIXMULTIPLY& data);
std::ostream& operator<<(std::ostream& os, D3DOPCODE data);
std::ostream& operator<<(std::ostream& os, const D3DPOINT& data);
std::ostream& operator<<(std::ostream& os, const D3DPROCESSVERTICES& data);
std::ostream& operator<<(std::ostream& os, const D3DRECT& data);
std::ostream& operator<<(std::ostream& os, D3DRENDERSTATETYPE data);
std::ostream& operator<<(std::ostream& os, const D3DSPAN& data);
std::ostream& operator<<(std::ostream& os, const D3DSTATUS& data);
std::ostream& operator<<(std::ostream& os, const D3DTEXTURELOAD& data);
std::ostream& operator<<(std::ostream& os, const D3DTLVERTEX& data);
std::ostream& operator<<(std::ostream& os, const D3DTRANSFORMDATA& data);
std::ostream& operator<<(std::ostream& os, D3DTRANSFORMSTATETYPE data);
std::ostream& operator<<(std::ostream& os, const D3DTRIANGLE& data);
std::ostream& operator<<(std::ostream& os, const D3DVERTEX& data);
std::ostream& operator<<(std::ostream& os, const D3DVERTEXBUFFERDESC& data);
std::ostream& operator<<(std::ostream& os, const D3DVIEWPORT& data);
std::ostream& operator<<(std::ostream& os, const D3DVIEWPORT2& data);
std::ostream& operator<<(std::ostream& os, const D3DVIEWPORT7& data);
