#include <Direct3d/Log.h>

namespace
{
	struct D3DLIGHTSTATE : D3DSTATE {};
	struct D3DRENDERSTATE : D3DSTATE {};
	struct D3DTRANSFORMSTATE : D3DSTATE {};

	std::ostream& operator<<(std::ostream& os, const D3DLIGHTSTATE& data)
	{
		return Compat::LogStruct(os)
			<< data.dlstLightStateType
			<< data.dwArg[0];
	}

	std::ostream& operator<<(std::ostream& os, const D3DRENDERSTATE& data)
	{
		return Compat::LogStruct(os)
			<< data.drstRenderStateType
			<< data.dwArg[0];
	}

	std::ostream& operator<<(std::ostream& os, const D3DTRANSFORMSTATE& data)
	{
		return Compat::LogStruct(os)
			<< data.dtstTransformStateType
			<< data.dwArg[0];
	}

	template <typename Data>
	std::ostream& logInstructionData(Compat::LogStruct& log, const D3DINSTRUCTION& data)
	{
		if (sizeof(Data) == data.bSize)
		{
			log << Compat::array(reinterpret_cast<const Data*>(&data + 1), data.wCount);
		}
		return log;
	}
}

std::ostream& operator<<(std::ostream& os, const D3DBRANCH& data)
{
	return Compat::LogStruct(os)
		<< Compat::hex(data.dwMask)
		<< Compat::hex(data.dwValue)
		<< data.bNegate
		<< data.dwOffset;
}

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

std::ostream& operator<<(std::ostream& os, const D3DINSTRUCTION& data)
{
	auto& log = Compat::LogStruct(os)
		<< static_cast<D3DOPCODE>(data.bOpcode)
		<< static_cast<DWORD>(data.bSize)
		<< data.wCount;

	switch (data.bOpcode)
	{
	case D3DOP_POINT:
		return logInstructionData<D3DPOINT>(log, data);
	case D3DOP_LINE:
		return logInstructionData<D3DLINE>(log, data);
	case D3DOP_TRIANGLE:
		return logInstructionData<D3DTRIANGLE>(log, data);
	case D3DOP_MATRIXLOAD:
		return logInstructionData<D3DMATRIXLOAD>(log, data);
	case D3DOP_MATRIXMULTIPLY:
		return logInstructionData<D3DMATRIXMULTIPLY>(log, data);
	case D3DOP_STATETRANSFORM:
		return logInstructionData<D3DTRANSFORMSTATE>(log, data);
	case D3DOP_STATELIGHT:
		return logInstructionData<D3DLIGHTSTATE>(log, data);
	case D3DOP_STATERENDER:
		return logInstructionData<D3DRENDERSTATE>(log, data);
	case D3DOP_PROCESSVERTICES:
		return logInstructionData<D3DPROCESSVERTICES>(log, data);
	case D3DOP_TEXTURELOAD:
		return logInstructionData<D3DTEXTURELOAD>(log, data);
	case D3DOP_BRANCHFORWARD:
		return logInstructionData<D3DBRANCH>(log, data);
	case D3DOP_SPAN:
		return logInstructionData<D3DSPAN>(log, data);
	case D3DOP_SETSTATUS:
		return logInstructionData<D3DSTATUS>(log, data);
	}

	return log;
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

std::ostream& operator<<(std::ostream& os, D3DLIGHTSTATETYPE data)
{
	switch (data)
	{
		LOG_CONST_CASE(D3DLIGHTSTATE_MATERIAL);
		LOG_CONST_CASE(D3DLIGHTSTATE_AMBIENT);
		LOG_CONST_CASE(D3DLIGHTSTATE_COLORMODEL);
		LOG_CONST_CASE(D3DLIGHTSTATE_FOGMODE);
		LOG_CONST_CASE(D3DLIGHTSTATE_FOGSTART);
		LOG_CONST_CASE(D3DLIGHTSTATE_FOGEND);
		LOG_CONST_CASE(D3DLIGHTSTATE_FOGDENSITY);
		LOG_CONST_CASE(D3DLIGHTSTATE_COLORVERTEX);
	}
	return os << "D3DLIGHTSTATE_" << static_cast<DWORD>(data);
}

std::ostream& operator<<(std::ostream& os, const D3DLINE& data)
{
	return Compat::LogStruct(os)
		<< data.v1
		<< data.v2;
}

std::ostream& operator<<(std::ostream& os, const D3DLVERTEX& data)
{
	return Compat::LogStruct(os)
		<< data.x
		<< data.y
		<< data.z
		<< data.dwReserved
		<< Compat::hex(data.color)
		<< Compat::hex(data.specular)
		<< data.tu
		<< data.tv;
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

std::ostream& operator<<(std::ostream& os, const D3DMATRIXLOAD& data)
{
	return Compat::LogStruct(os)
		<< data.hDestMatrix
		<< data.hSrcMatrix;
}

std::ostream& operator<<(std::ostream& os, const D3DMATRIXMULTIPLY& data)
{
	return Compat::LogStruct(os)
		<< data.hDestMatrix
		<< data.hSrcMatrix1
		<< data.hSrcMatrix2;
}

std::ostream& operator<<(std::ostream& os, D3DOPCODE data)
{
	switch (data)
	{
		LOG_CONST_CASE(D3DOP_POINT);
		LOG_CONST_CASE(D3DOP_LINE);
		LOG_CONST_CASE(D3DOP_TRIANGLE);
		LOG_CONST_CASE(D3DOP_MATRIXLOAD);
		LOG_CONST_CASE(D3DOP_MATRIXMULTIPLY);
		LOG_CONST_CASE(D3DOP_STATETRANSFORM);
		LOG_CONST_CASE(D3DOP_STATELIGHT);
		LOG_CONST_CASE(D3DOP_STATERENDER);
		LOG_CONST_CASE(D3DOP_PROCESSVERTICES);
		LOG_CONST_CASE(D3DOP_TEXTURELOAD);
		LOG_CONST_CASE(D3DOP_EXIT);
		LOG_CONST_CASE(D3DOP_BRANCHFORWARD);
		LOG_CONST_CASE(D3DOP_SPAN);
		LOG_CONST_CASE(D3DOP_SETSTATUS);
	}
	return os << "D3DOP_" << static_cast<DWORD>(data);
}

std::ostream& operator<<(std::ostream& os, const D3DPOINT& data)
{
	return Compat::LogStruct(os)
		<< data.wCount
		<< data.wFirst;
}

std::ostream& operator<<(std::ostream& os, const D3DPROCESSVERTICES& data)
{
	return Compat::LogStruct(os)
		<< Compat::hex(data.dwFlags)
		<< data.wStart
		<< data.wDest
		<< data.dwCount
		<< data.dwReserved;
}

std::ostream& operator<<(std::ostream& os, const D3DRECT& data)
{
	return Compat::LogStruct(os)
		<< data.x1
		<< data.y1
		<< data.x2
		<< data.y2;
}

std::ostream& operator<<(std::ostream& os, D3DRENDERSTATETYPE data)
{
	switch (data)
	{
		LOG_CONST_CASE(D3DRENDERSTATE_TEXTUREHANDLE);
		LOG_CONST_CASE(D3DRENDERSTATE_ANTIALIAS);
		LOG_CONST_CASE(D3DRENDERSTATE_TEXTUREADDRESS);
		LOG_CONST_CASE(D3DRENDERSTATE_TEXTUREPERSPECTIVE);
		LOG_CONST_CASE(D3DRENDERSTATE_WRAPU);
		LOG_CONST_CASE(D3DRENDERSTATE_WRAPV);
		LOG_CONST_CASE(D3DRENDERSTATE_ZENABLE);
		LOG_CONST_CASE(D3DRENDERSTATE_FILLMODE);
		LOG_CONST_CASE(D3DRENDERSTATE_SHADEMODE);
		LOG_CONST_CASE(D3DRENDERSTATE_LINEPATTERN);
		LOG_CONST_CASE(D3DRENDERSTATE_MONOENABLE);
		LOG_CONST_CASE(D3DRENDERSTATE_ROP2);
		LOG_CONST_CASE(D3DRENDERSTATE_PLANEMASK);
		LOG_CONST_CASE(D3DRENDERSTATE_ZWRITEENABLE);
		LOG_CONST_CASE(D3DRENDERSTATE_ALPHATESTENABLE);
		LOG_CONST_CASE(D3DRENDERSTATE_LASTPIXEL);
		LOG_CONST_CASE(D3DRENDERSTATE_TEXTUREMAG);
		LOG_CONST_CASE(D3DRENDERSTATE_TEXTUREMIN);
		LOG_CONST_CASE(D3DRENDERSTATE_SRCBLEND);
		LOG_CONST_CASE(D3DRENDERSTATE_DESTBLEND);
		LOG_CONST_CASE(D3DRENDERSTATE_TEXTUREMAPBLEND);
		LOG_CONST_CASE(D3DRENDERSTATE_CULLMODE);
		LOG_CONST_CASE(D3DRENDERSTATE_ZFUNC);
		LOG_CONST_CASE(D3DRENDERSTATE_ALPHAREF);
		LOG_CONST_CASE(D3DRENDERSTATE_ALPHAFUNC);
		LOG_CONST_CASE(D3DRENDERSTATE_DITHERENABLE);
		LOG_CONST_CASE(D3DRENDERSTATE_ALPHABLENDENABLE);
		LOG_CONST_CASE(D3DRENDERSTATE_FOGENABLE);
		LOG_CONST_CASE(D3DRENDERSTATE_SPECULARENABLE);
		LOG_CONST_CASE(D3DRENDERSTATE_ZVISIBLE);
		LOG_CONST_CASE(D3DRENDERSTATE_SUBPIXEL);
		LOG_CONST_CASE(D3DRENDERSTATE_SUBPIXELX);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEDALPHA);
		LOG_CONST_CASE(D3DRENDERSTATE_FOGCOLOR);
		LOG_CONST_CASE(D3DRENDERSTATE_FOGTABLEMODE);
		LOG_CONST_CASE(D3DRENDERSTATE_FOGSTART);
		LOG_CONST_CASE(D3DRENDERSTATE_FOGEND);
		LOG_CONST_CASE(D3DRENDERSTATE_FOGDENSITY);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEENABLE);
		LOG_CONST_CASE(D3DRENDERSTATE_EDGEANTIALIAS);
		LOG_CONST_CASE(D3DRENDERSTATE_COLORKEYENABLE);
		LOG_CONST_CASE(D3DRENDERSTATE_BORDERCOLOR);
		LOG_CONST_CASE(D3DRENDERSTATE_TEXTUREADDRESSU);
		LOG_CONST_CASE(D3DRENDERSTATE_TEXTUREADDRESSV);
		LOG_CONST_CASE(D3DRENDERSTATE_MIPMAPLODBIAS);
		LOG_CONST_CASE(D3DRENDERSTATE_ZBIAS);
		LOG_CONST_CASE(D3DRENDERSTATE_RANGEFOGENABLE);
		LOG_CONST_CASE(D3DRENDERSTATE_ANISOTROPY);
		LOG_CONST_CASE(D3DRENDERSTATE_FLUSHBATCH);
		LOG_CONST_CASE(D3DRENDERSTATE_TRANSLUCENTSORTINDEPENDENT);
		LOG_CONST_CASE(D3DRENDERSTATE_STENCILENABLE);
		LOG_CONST_CASE(D3DRENDERSTATE_STENCILFAIL);
		LOG_CONST_CASE(D3DRENDERSTATE_STENCILZFAIL);
		LOG_CONST_CASE(D3DRENDERSTATE_STENCILPASS);
		LOG_CONST_CASE(D3DRENDERSTATE_STENCILFUNC);
		LOG_CONST_CASE(D3DRENDERSTATE_STENCILREF);
		LOG_CONST_CASE(D3DRENDERSTATE_STENCILMASK);
		LOG_CONST_CASE(D3DRENDERSTATE_STENCILWRITEMASK);
		LOG_CONST_CASE(D3DRENDERSTATE_TEXTUREFACTOR);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN00);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN01);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN02);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN03);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN04);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN05);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN06);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN07);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN08);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN09);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN10);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN11);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN12);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN13);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN14);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN15);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN16);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN17);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN18);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN19);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN20);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN21);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN22);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN23);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN24);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN25);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN26);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN27);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN28);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN29);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN30);
		LOG_CONST_CASE(D3DRENDERSTATE_STIPPLEPATTERN31);
		LOG_CONST_CASE(D3DRENDERSTATE_WRAP0);
		LOG_CONST_CASE(D3DRENDERSTATE_WRAP1);
		LOG_CONST_CASE(D3DRENDERSTATE_WRAP2);
		LOG_CONST_CASE(D3DRENDERSTATE_WRAP3);
		LOG_CONST_CASE(D3DRENDERSTATE_WRAP4);
		LOG_CONST_CASE(D3DRENDERSTATE_WRAP5);
		LOG_CONST_CASE(D3DRENDERSTATE_WRAP6);
		LOG_CONST_CASE(D3DRENDERSTATE_WRAP7);
		LOG_CONST_CASE(D3DRENDERSTATE_CLIPPING);
		LOG_CONST_CASE(D3DRENDERSTATE_LIGHTING);
		LOG_CONST_CASE(D3DRENDERSTATE_EXTENTS);
		LOG_CONST_CASE(D3DRENDERSTATE_AMBIENT);
		LOG_CONST_CASE(D3DRENDERSTATE_FOGVERTEXMODE);
		LOG_CONST_CASE(D3DRENDERSTATE_COLORVERTEX);
		LOG_CONST_CASE(D3DRENDERSTATE_LOCALVIEWER);
		LOG_CONST_CASE(D3DRENDERSTATE_NORMALIZENORMALS);
		LOG_CONST_CASE(D3DRENDERSTATE_COLORKEYBLENDENABLE);
		LOG_CONST_CASE(D3DRENDERSTATE_DIFFUSEMATERIALSOURCE);
		LOG_CONST_CASE(D3DRENDERSTATE_SPECULARMATERIALSOURCE);
		LOG_CONST_CASE(D3DRENDERSTATE_AMBIENTMATERIALSOURCE);
		LOG_CONST_CASE(D3DRENDERSTATE_EMISSIVEMATERIALSOURCE);
		LOG_CONST_CASE(D3DRENDERSTATE_VERTEXBLEND);
		LOG_CONST_CASE(D3DRENDERSTATE_CLIPPLANEENABLE);
	}
	return os << "D3DRENDERSTATE_" << static_cast<DWORD>(data);
}

std::ostream& operator<<(std::ostream& os, const D3DSPAN& data)
{
	return Compat::LogStruct(os)
		<< data.wCount
		<< data.wFirst;
}

std::ostream& operator<<(std::ostream& os, const D3DSTATUS& data)
{
	return Compat::LogStruct(os)
		<< Compat::hex(data.dwFlags)
		<< Compat::hex(data.dwStatus)
		<< data.drExtent;
}

std::ostream& operator<<(std::ostream& os, const D3DTEXTURELOAD& data)
{
	return Compat::LogStruct(os)
		<< data.hDestTexture
		<< data.hSrcTexture;
}

std::ostream& operator<<(std::ostream& os, const D3DTLVERTEX& data)
{
	return Compat::LogStruct(os)
		<< data.sx
		<< data.sy
		<< data.sz
		<< data.rhw
		<< Compat::hex(data.color)
		<< Compat::hex(data.specular)
		<< data.tu
		<< data.tv;
}

std::ostream& operator<<(std::ostream& os, const D3DTRANSFORMDATA& data)
{
	return Compat::LogStruct(os)
		<< data.lpIn
		<< data.dwInSize
		<< data.lpOut
		<< data.dwOutSize
		<< data.lpHOut
		<< Compat::hex(data.dwClip)
		<< Compat::hex(data.dwClipIntersection)
		<< Compat::hex(data.dwClipUnion)
		<< data.drExtent;
}

std::ostream& operator<<(std::ostream& os, D3DTRANSFORMSTATETYPE data)
{
	switch (data)
	{
		LOG_CONST_CASE(D3DTRANSFORMSTATE_WORLD);
		LOG_CONST_CASE(D3DTRANSFORMSTATE_VIEW);
		LOG_CONST_CASE(D3DTRANSFORMSTATE_PROJECTION);
		LOG_CONST_CASE(D3DTRANSFORMSTATE_WORLD1);
		LOG_CONST_CASE(D3DTRANSFORMSTATE_WORLD2);
		LOG_CONST_CASE(D3DTRANSFORMSTATE_WORLD3);
		LOG_CONST_CASE(D3DTRANSFORMSTATE_TEXTURE0);
		LOG_CONST_CASE(D3DTRANSFORMSTATE_TEXTURE1);
		LOG_CONST_CASE(D3DTRANSFORMSTATE_TEXTURE2);
		LOG_CONST_CASE(D3DTRANSFORMSTATE_TEXTURE3);
		LOG_CONST_CASE(D3DTRANSFORMSTATE_TEXTURE4);
		LOG_CONST_CASE(D3DTRANSFORMSTATE_TEXTURE5);
		LOG_CONST_CASE(D3DTRANSFORMSTATE_TEXTURE6);
		LOG_CONST_CASE(D3DTRANSFORMSTATE_TEXTURE7);
	}
	return os << "D3DTRANSFORMSTATE_" << static_cast<DWORD>(data);
}

std::ostream& operator<<(std::ostream& os, const D3DTRIANGLE& data)
{
	return Compat::LogStruct(os)
		<< data.v1
		<< data.v2
		<< data.v3;
}

std::ostream& operator<<(std::ostream& os, const D3DVERTEX& data)
{
	return Compat::LogStruct(os)
		<< data.x
		<< data.y
		<< data.z
		<< data.nx
		<< data.ny
		<< data.nz
		<< data.tu
		<< data.tv;
}

std::ostream& operator<<(std::ostream& os, const D3DVERTEXBUFFERDESC& data)
{
	return Compat::LogStruct(os)
		<< Compat::hex(data.dwCaps)
		<< Compat::hex(data.dwFVF)
		<< data.dwNumVertices;
}

std::ostream& operator<<(std::ostream& os, const D3DVIEWPORT& data)
{
	return Compat::LogStruct(os)
		<< data.dwX
		<< data.dwY
		<< data.dwWidth
		<< data.dwHeight
		<< data.dvScaleX
		<< data.dvScaleY
		<< data.dvMaxX
		<< data.dvMaxY
		<< data.dvMinZ
		<< data.dvMaxZ;
}

std::ostream& operator<<(std::ostream& os, const D3DVIEWPORT2& data)
{
	return Compat::LogStruct(os)
		<< data.dwX
		<< data.dwY
		<< data.dwWidth
		<< data.dwHeight
		<< data.dvClipX
		<< data.dvClipY
		<< data.dvClipWidth
		<< data.dvClipHeight
		<< data.dvMinZ
		<< data.dvMaxZ;
}

std::ostream& operator<<(std::ostream& os, const D3DVIEWPORT7& data)
{
	return Compat::LogStruct(os)
		<< data.dwX
		<< data.dwY
		<< data.dwWidth
		<< data.dwHeight
		<< data.dvMinZ
		<< data.dvMaxZ;
}
