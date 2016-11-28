#define CINTERFACE

#include <map>

#include <d3d.h>
#include <d3dumddi.h>
#include <../km/d3dkmthk.h>

#include "Common/Log.h"
#include "Common/Hook.h"
#include "D3dDdi/Hooks.h"
#include "D3dDdi/KernelModeThunks.h"
#include "DDraw/Surfaces/PrimarySurface.h"

namespace
{
	struct ContextInfo
	{
		D3DKMT_HANDLE device;

		ContextInfo() : device(0) {}
	};

	struct DeviceInfo
	{
		D3DKMT_HANDLE adapter;
		D3DDDI_VIDEO_PRESENT_SOURCE_ID vidPnSourceId;

		DeviceInfo() : adapter(0), vidPnSourceId(D3DDDI_ID_UNINITIALIZED) {}
	};

	std::map<D3DKMT_HANDLE, ContextInfo> g_contexts;
	std::map<D3DKMT_HANDLE, DeviceInfo> g_devices;

	NTSTATUS APIENTRY createDevice(D3DKMT_CREATEDEVICE* pData)
	{
		Compat::LogEnter("D3DKMTCreateDevice", pData);
		NTSTATUS result = CALL_ORIG_FUNC(D3DKMTCreateDevice)(pData);
		if (SUCCEEDED(result))
		{
			g_devices[pData->hDevice].adapter = pData->hAdapter;

			D3DKMT_SETQUEUEDLIMIT limit = {};
			limit.hDevice = pData->hDevice;
			limit.Type = D3DKMT_SET_QUEUEDLIMIT_PRESENT;
			limit.QueuedPresentLimit = 1;
			CALL_ORIG_FUNC(D3DKMTSetQueuedLimit)(&limit);
		}
		Compat::LogLeave("D3DKMTCreateDevice", pData) << result;
		return result;
	}

	NTSTATUS APIENTRY createContext(D3DKMT_CREATECONTEXT* pData)
	{
		Compat::LogEnter("D3DKMTCreateContext", pData);
		NTSTATUS result = CALL_ORIG_FUNC(D3DKMTCreateContext)(pData);
		if (SUCCEEDED(result))
		{
			g_contexts[pData->hContext].device = pData->hDevice;
		}
		Compat::LogLeave("D3DKMTCreateContext", pData) << result;
		return result;
	}

	NTSTATUS APIENTRY createContextVirtual(D3DKMT_CREATECONTEXTVIRTUAL* pData)
	{
		Compat::LogEnter("D3DKMTCreateContextVirtual", pData);
		NTSTATUS result = CALL_ORIG_FUNC(D3DKMTCreateContextVirtual)(pData);
		if (SUCCEEDED(result))
		{
			g_contexts[pData->hContext].device = pData->hDevice;
		}
		Compat::LogLeave("D3DKMTCreateContextVirtual", pData) << result;
		return result;
	}

	NTSTATUS APIENTRY destroyContext(const D3DKMT_DESTROYCONTEXT* pData)
	{
		Compat::LogEnter("D3DKMTDestroyContext", pData);
		NTSTATUS result = CALL_ORIG_FUNC(D3DKMTDestroyContext)(pData);
		if (SUCCEEDED(result))
		{
			g_contexts.erase(pData->hContext);
		}
		Compat::LogLeave("D3DKMTDestroyContext", pData) << result;
		return result;
	}

	NTSTATUS APIENTRY destroyDevice(const D3DKMT_DESTROYDEVICE* pData)
	{
		Compat::LogEnter("D3DKMTDestroyDevice", pData);
		NTSTATUS result = CALL_ORIG_FUNC(D3DKMTDestroyDevice)(pData);
		if (SUCCEEDED(result))
		{
			g_devices.erase(pData->hDevice);
		}
		Compat::LogLeave("D3DKMTDestroyDevice", pData) << result;
		return result;
	}

	NTSTATUS APIENTRY present(D3DKMT_PRESENT* pData)
	{
		Compat::LogEnter("D3DKMTPresent", pData);

		static UINT presentCount = 0;
		++presentCount;
		pData->Flags.PresentCountValid = 1;
		pData->PresentCount = presentCount;

		NTSTATUS result = CALL_ORIG_FUNC(D3DKMTPresent)(pData);
		if (SUCCEEDED(result) &&
			1 == DDraw::PrimarySurface::getDesc().dwBackBufferCount &&
			pData->Flags.Flip && pData->FlipInterval != D3DDDI_FLIPINTERVAL_IMMEDIATE)
		{
			auto contextIt = g_contexts.find(pData->hContext);
			auto deviceIt = (contextIt != g_contexts.end())
				? g_devices.find(contextIt->second.device)
				: g_devices.find(pData->hDevice);
			if (deviceIt != g_devices.end())
			{
				D3DKMT_WAITFORVERTICALBLANKEVENT vbEvent = {};
				vbEvent.hAdapter = deviceIt->second.adapter;
				vbEvent.hDevice = deviceIt->first;
				vbEvent.VidPnSourceId = deviceIt->second.vidPnSourceId;

				D3DKMT_GETDEVICESTATE deviceState = {};
				deviceState.hDevice = deviceIt->first;
				deviceState.StateType = D3DKMT_DEVICESTATE_PRESENT;
				deviceState.PresentState.VidPnSourceId = deviceIt->second.vidPnSourceId;
				NTSTATUS stateResult = D3DKMTGetDeviceState(&deviceState);
				while (SUCCEEDED(stateResult) &&
					presentCount != deviceState.PresentState.PresentStats.PresentCount)
				{
					if (FAILED(D3DKMTWaitForVerticalBlankEvent(&vbEvent)))
					{
						Sleep(1);
					}
					stateResult = D3DKMTGetDeviceState(&deviceState);
				}
			}
		}

		Compat::LogLeave("D3DKMTPresent", pData) << result;
		return result;
	}

	NTSTATUS APIENTRY queryAdapterInfo(const D3DKMT_QUERYADAPTERINFO* pData)
	{
		Compat::LogEnter("D3DKMTQueryAdapterInfo", pData);
		NTSTATUS result = CALL_ORIG_FUNC(D3DKMTQueryAdapterInfo)(pData);
		if (SUCCEEDED(result) && KMTQAITYPE_UMDRIVERNAME == pData->Type)
		{
			auto info = static_cast<D3DKMT_UMDFILENAMEINFO*>(pData->pPrivateDriverData);
			D3dDdi::onUmdFileNameQueried(info->UmdFileName);
		}
		Compat::LogLeave("D3DKMTQueryAdapterInfo", pData) << result;
		return result;
	}

	NTSTATUS APIENTRY setQueuedLimit(const D3DKMT_SETQUEUEDLIMIT* pData)
	{
		Compat::LogEnter("D3DKMTSetQueuedLimit", pData);
		if (D3DKMT_SET_QUEUEDLIMIT_PRESENT == pData->Type)
		{
			const UINT origLimit = pData->QueuedPresentLimit;
			const_cast<D3DKMT_SETQUEUEDLIMIT*>(pData)->QueuedPresentLimit = 1;
			NTSTATUS result = CALL_ORIG_FUNC(D3DKMTSetQueuedLimit)(pData);
			const_cast<D3DKMT_SETQUEUEDLIMIT*>(pData)->QueuedPresentLimit = origLimit;
			Compat::LogLeave("D3DKMTSetQueuedLimit", pData) << result;
			return result;
		}
		NTSTATUS result = CALL_ORIG_FUNC(D3DKMTSetQueuedLimit)(pData);
		Compat::LogLeave("D3DKMTSetQueuedLimit", pData) << result;
		return result;
	}

	void processSetVidPnSourceOwner(const D3DKMT_SETVIDPNSOURCEOWNER* pData)
	{
		auto& vidPnSourceId = g_devices[pData->hDevice].vidPnSourceId;
		for (UINT i = 0; i < pData->VidPnSourceCount; ++i)
		{
			if (D3DKMT_VIDPNSOURCEOWNER_UNOWNED != pData->pType[i])
			{
				vidPnSourceId = pData->pVidPnSourceId[i];
				return;
			}
		}
		vidPnSourceId = D3DDDI_ID_UNINITIALIZED;
	}

	NTSTATUS APIENTRY setVidPnSourceOwner(const D3DKMT_SETVIDPNSOURCEOWNER* pData)
	{
		Compat::LogEnter("D3DKMTSetVidPnSourceOwner", pData);
		NTSTATUS result = CALL_ORIG_FUNC(D3DKMTSetVidPnSourceOwner)(pData);
		if (SUCCEEDED(result))
		{
			processSetVidPnSourceOwner(pData);
		}
		Compat::LogLeave("D3DKMTSetVidPnSourceOwner", pData) << result;
		return result;
	}

	NTSTATUS APIENTRY setVidPnSourceOwner1(const D3DKMT_SETVIDPNSOURCEOWNER1* pData)
	{
		Compat::LogEnter("D3DKMTSetVidPnSourceOwner1", pData);
		NTSTATUS result = CALL_ORIG_FUNC(D3DKMTSetVidPnSourceOwner1)(pData);
		if (SUCCEEDED(result))
		{
			processSetVidPnSourceOwner(&pData->Version0);
		}
		Compat::LogLeave("D3DKMTSetVidPnSourceOwner1", pData) << result;
		return result;
	}
}

namespace D3dDdi
{
	namespace KernelModeThunks
	{
		void installHooks()
		{
			HOOK_FUNCTION(gdi32, D3DKMTCreateContext, createContext);
			HOOK_FUNCTION(gdi32, D3DKMTCreateContextVirtual, createContextVirtual);
			HOOK_FUNCTION(gdi32, D3DKMTCreateDevice, createDevice);
			HOOK_FUNCTION(gdi32, D3DKMTDestroyContext, destroyContext);
			HOOK_FUNCTION(gdi32, D3DKMTDestroyDevice, destroyDevice);
			HOOK_FUNCTION(gdi32, D3DKMTQueryAdapterInfo, queryAdapterInfo);
			HOOK_FUNCTION(gdi32, D3DKMTPresent, present);
			HOOK_FUNCTION(gdi32, D3DKMTSetQueuedLimit, setQueuedLimit);
			HOOK_FUNCTION(gdi32, D3DKMTSetVidPnSourceOwner, setVidPnSourceOwner);
			HOOK_FUNCTION(gdi32, D3DKMTSetVidPnSourceOwner1, setVidPnSourceOwner1);
		}
	}
}
