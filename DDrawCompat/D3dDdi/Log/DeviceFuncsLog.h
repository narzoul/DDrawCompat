#pragma once

#include <ostream>

#include <d3d.h>
#include <d3dumddi.h>

#include <D3dDdi/Log/CommonLog.h>

std::ostream& operator<<(std::ostream& os, D3DDDI_POOL val);
std::ostream& operator<<(std::ostream& os, const D3DDDI_OPENALLOCATIONINFO& val);
std::ostream& operator<<(std::ostream& os, const D3DDDI_SURFACEINFO& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_BLT& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_CLEAR& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_COLORFILL& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_CREATERESOURCE& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_CREATERESOURCE2& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_CREATEVERTEXSHADERDECL& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_DEPTHFILL& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_DRAWINDEXEDPRIMITIVE& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_DRAWINDEXEDPRIMITIVE2& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_DRAWPRIMITIVE& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_DRAWPRIMITIVE2& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_LOCK& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_OPENRESOURCE& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_PRESENT& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_PRESENT1& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_PRESENTSURFACE& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_RENDERSTATE& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_SETDEPTHSTENCIL& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_SETPALETTE& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_SETPIXELSHADERCONST& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_SETRENDERTARGET& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_SETSTREAMSOURCE& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_SETSTREAMSOURCEUM& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_SETVERTEXSHADERCONST& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_TEXBLT& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_TEXBLT1& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_TEXTURESTAGESTATE& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_UNLOCK& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_UPDATEPALETTE& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_VALIDATETEXTURESTAGESTATE& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_VIEWPORTINFO& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_WINFO& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_ZRANGE& val);
std::ostream& operator<<(std::ostream& os, const D3DDDIBOX& val);
std::ostream& operator<<(std::ostream& os, D3DDDIRENDERSTATETYPE val);
std::ostream& operator<<(std::ostream& os, D3DDDITEXTURESTAGESTATETYPE val);
std::ostream& operator<<(std::ostream& os, const D3DDDIVERTEXELEMENT& val);
