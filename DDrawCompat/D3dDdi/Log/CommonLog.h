#pragma once

#include <ostream>

#include <d3d.h>
#include <d3dumddi.h>

namespace D3dDdi
{
	extern UINT g_umdVersion;
}

std::ostream& operator<<(std::ostream& os, const D3DDDI_ALLOCATIONLIST& data);
std::ostream& operator<<(std::ostream& os, const D3DDDI_GAMMA_RAMP_RGB256x3x16& data);
std::ostream& operator<<(std::ostream& os, const D3DDDI_PATCHLOCATIONLIST& data);
std::ostream& operator<<(std::ostream& os, const D3DDDI_RATIONAL& val);
std::ostream& operator<<(std::ostream& os, D3DDDIFORMAT val);
