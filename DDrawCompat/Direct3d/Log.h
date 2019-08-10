#pragma once

#include <d3d.h>

#include <Common/Log.h>

std::ostream& operator<<(std::ostream& os, const D3DDP_PTRSTRIDE& data);
std::ostream& operator<<(std::ostream& os, const D3DDRAWPRIMITIVESTRIDEDDATA& data);
std::ostream& operator<<(std::ostream& os, const D3DVERTEXBUFFERDESC& data);
