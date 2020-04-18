#pragma once

#include <d3d.h>

#include <Common/Log.h>

std::ostream& operator<<(std::ostream& os, const D3DCOLORVALUE& data);
std::ostream& operator<<(std::ostream& os, const D3DDP_PTRSTRIDE& data);
std::ostream& operator<<(std::ostream& os, const D3DDRAWPRIMITIVESTRIDEDDATA& data);
std::ostream& operator<<(std::ostream& os, const D3DEXECUTEBUFFERDESC& data);
std::ostream& operator<<(std::ostream& os, const D3DEXECUTEDATA& data);
std::ostream& operator<<(std::ostream& os, const D3DLIGHT& data);
std::ostream& operator<<(std::ostream& os, const D3DLIGHT2& data);
std::ostream& operator<<(std::ostream& os, const D3DLIGHT7& data);
std::ostream& operator<<(std::ostream& os, const D3DMATERIAL& data);
std::ostream& operator<<(std::ostream& os, const D3DMATERIAL7& data);
std::ostream& operator<<(std::ostream& os, const D3DRECT& data);
std::ostream& operator<<(std::ostream& os, const D3DSTATUS& data);
std::ostream& operator<<(std::ostream& os, const D3DVERTEXBUFFERDESC& data);
