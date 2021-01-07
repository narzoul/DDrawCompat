#include <guiddef.h>
#include <d3dnthal.h>

#include <map>

#include <D3dDdi/Adapter.h>
#include <D3dDdi/AdapterFuncs.h>
#include <D3dDdi/DeviceCallbacks.h>
#include <D3dDdi/DeviceFuncs.h>

namespace
{
	DWORD getSupportedZBufferBitDepths(HANDLE adapter);

	std::string bitDepthsToString(DWORD bitDepths)
	{
		std::string result;
		if (bitDepths & DDBD_8) { result += ", 8"; }
		if (bitDepths & DDBD_16) { result += ", 16"; }
		if (bitDepths & DDBD_24) { result += ", 24"; }
		if (bitDepths & DDBD_32) { result += ", 32"; }

		if (result.empty())
		{
			return "\"\"";
		}
		return '"' + result.substr(2) + '"';
	}

	HRESULT APIENTRY closeAdapter(HANDLE hAdapter)
	{
		HRESULT result = D3dDdi::AdapterFuncs::s_origVtablePtr->pfnCloseAdapter(hAdapter);
		if (SUCCEEDED(result))
		{
			D3dDdi::Adapter::remove(hAdapter);
		}
		return result;
	}

	HRESULT APIENTRY createDevice(HANDLE hAdapter, D3DDDIARG_CREATEDEVICE* pCreateData)
	{
		D3dDdi::DeviceCallbacks::hookVtable(pCreateData->pCallbacks);
		HRESULT result = D3dDdi::AdapterFuncs::s_origVtablePtr->pfnCreateDevice(hAdapter, pCreateData);
		if (SUCCEEDED(result))
		{
			D3dDdi::DeviceFuncs::hookVtable(
				D3dDdi::Adapter::get(hAdapter).getModule(), pCreateData->pDeviceFuncs);
			D3dDdi::DeviceFuncs::onCreateDevice(hAdapter, pCreateData->hDevice);
		}
		return result;
	}

	HRESULT APIENTRY getCaps(HANDLE hAdapter, const D3DDDIARG_GETCAPS* pData)
	{
		HRESULT result = D3dDdi::AdapterFuncs::s_origVtablePtr->pfnGetCaps(hAdapter, pData);
		if (FAILED(result))
		{
			return result;
		}

		switch (pData->Type)
		{
		case D3DDDICAPS_DDRAW:
			static_cast<DDRAW_CAPS*>(pData->pData)->FxCaps =
				DDRAW_FXCAPS_BLTMIRRORLEFTRIGHT | DDRAW_FXCAPS_BLTMIRRORUPDOWN;
			break;

		case D3DDDICAPS_GETD3D3CAPS:
		{
			auto& caps = static_cast<D3DNTHAL_GLOBALDRIVERDATA*>(pData->pData)->hwCaps;
			if (caps.dwFlags & D3DDD_DEVICEZBUFFERBITDEPTH)
			{
				const DWORD supportedZBufferBitDepths = getSupportedZBufferBitDepths(hAdapter);
				if (supportedZBufferBitDepths != caps.dwDeviceZBufferBitDepth)
				{
					LOG_ONCE("Incorrect z-buffer bit depth capabilities detected; changed from "
						<< bitDepthsToString(caps.dwDeviceZBufferBitDepth) << " to "
						<< bitDepthsToString(supportedZBufferBitDepths));
					caps.dwDeviceZBufferBitDepth = supportedZBufferBitDepths;
				}
			}
			break;
		}
		}

		return result;
	}

	DWORD getSupportedZBufferBitDepths(HANDLE adapter)
	{
		UINT formatCount = 0;
		D3DDDIARG_GETCAPS caps = {};
		caps.Type = D3DDDICAPS_GETFORMATCOUNT;
		caps.pData = &formatCount;
		caps.DataSize = sizeof(formatCount);
		D3dDdi::AdapterFuncs::s_origVtablePtr->pfnGetCaps(adapter, &caps);

		std::vector<FORMATOP> formatOp(formatCount);
		caps.Type = D3DDDICAPS_GETFORMATDATA;
		caps.pData = formatOp.data();
		caps.DataSize = formatCount * sizeof(FORMATOP);
		D3dDdi::AdapterFuncs::s_origVtablePtr->pfnGetCaps(adapter, &caps);

		DWORD supportedZBufferBitDepths = 0;
		for (UINT i = 0; i < formatCount; ++i)
		{
			if (formatOp[i].Operations & (FORMATOP_ZSTENCIL | FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH))
			{
				switch (formatOp[i].Format)
				{
				case D3DDDIFMT_D16:
					supportedZBufferBitDepths |= DDBD_16;
					break;

				case D3DDDIFMT_X8D24:
					supportedZBufferBitDepths |= DDBD_24;
					break;

				case D3DDDIFMT_D32:
					supportedZBufferBitDepths |= DDBD_32;
					break;
				}
			}
		}

		return supportedZBufferBitDepths;
	}
}

namespace D3dDdi
{
	void AdapterFuncs::onOpenAdapter(HANDLE adapter, HMODULE module)
	{
		Adapter::add(adapter, module);
	}

	void AdapterFuncs::setCompatVtable(D3DDDI_ADAPTERFUNCS& vtable)
	{
		vtable.pfnCloseAdapter = &closeAdapter;
		vtable.pfnCreateDevice = &createDevice;
		vtable.pfnGetCaps = &getCaps;
	}
}
