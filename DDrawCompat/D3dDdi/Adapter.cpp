#include <map>
#include <sstream>

#include <Common/Comparison.h>
#include <Common/CompatVtable.h>
#include <Config/Config.h>
#include <D3dDdi/Adapter.h>
#include <D3dDdi/AdapterFuncs.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/DeviceCallbacks.h>
#include <D3dDdi/DeviceFuncs.h>
#include <D3dDdi/KernelModeThunks.h>

namespace
{
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
}

namespace D3dDdi
{
	Adapter::Adapter(const D3DDDIARG_OPENADAPTER& data)
		: m_adapter(data.hAdapter)
		, m_origVtable(CompatVtable<D3DDDI_ADAPTERFUNCS>::s_origVtable)
		, m_runtimeVersion(data.Version)
		, m_driverVersion(data.DriverVersion)
		, m_luid(KernelModeThunks::getLastOpenAdapterInfo().luid)
		, m_repository{}
	{
	}

	const Adapter::AdapterInfo& Adapter::getInfo() const
	{
		auto it = s_adapterInfos.find(m_luid);
		if (it != s_adapterInfos.end())
		{
			return it->second;
		}

		AdapterInfo& info = s_adapterInfos.insert({ m_luid, {} }).first->second;
		getCaps(D3DDDICAPS_GETD3D7CAPS, info.d3dExtendedCaps);
		info.formatOps = getFormatOps();
		info.supportedZBufferBitDepths = getSupportedZBufferBitDepths(info.formatOps);

		Compat::Log() << "Supported z-buffer bit depths: " << bitDepthsToString(info.supportedZBufferBitDepths);
		Compat::Log() << "Supported MSAA modes: " << getSupportedMsaaModes(info.formatOps);

		return info;
	}

	template <typename Data>
	HRESULT Adapter::getCaps(D3DDDICAPS_TYPE type, Data& data, UINT size) const
	{
		D3DDDIARG_GETCAPS caps = {};
		caps.Type = type;
		caps.pData = &data;
		caps.DataSize = size;
		return m_origVtable.pfnGetCaps(m_adapter, &caps);
	}

	std::map<D3DDDIFORMAT, FORMATOP> Adapter::getFormatOps() const
	{
		UINT formatCount = 0;
		getCaps(D3DDDICAPS_GETFORMATCOUNT, formatCount);

		std::vector<FORMATOP> formatOps(formatCount);
		getCaps(D3DDDICAPS_GETFORMATDATA, formatOps[0], formatCount * sizeof(FORMATOP));

		std::map<D3DDDIFORMAT, FORMATOP> result;
		for (UINT i = 0; i < formatCount; ++i)
		{
			result[formatOps[i].Format] = formatOps[i];
		}
		return result;
	}

	std::pair<D3DDDIMULTISAMPLE_TYPE, UINT> Adapter::getMultisampleConfig(D3DDDIFORMAT format) const
	{
		UINT samples = Config::antialiasing.get();
		if (D3DDDIMULTISAMPLE_NONE == samples)
		{
			return { D3DDDIMULTISAMPLE_NONE, 0 };
		}

		const auto& info(getInfo());
		auto it = info.formatOps.find(format);
		if (it == info.formatOps.end() || 0 == it->second.BltMsTypes)
		{
			return { D3DDDIMULTISAMPLE_NONE, 0 };
		}

		while (samples > D3DDDIMULTISAMPLE_NONMASKABLE && !(it->second.BltMsTypes & (1 << (samples - 1))))
		{
			--samples;
		}

		DDIMULTISAMPLEQUALITYLEVELSDATA levels = {};
		levels.Format = D3DDDIFMT_X8R8G8B8;
		levels.MsType = static_cast<D3DDDIMULTISAMPLE_TYPE>(samples);
		getCaps(D3DDDICAPS_GETMULTISAMPLEQUALITYLEVELS, levels);
		return { levels.MsType, min(Config::antialiasing.getParam(), levels.QualityLevels - 1) };
	}

	std::string Adapter::getSupportedMsaaModes(const std::map<D3DDDIFORMAT, FORMATOP>& formatOps) const
	{
		auto it = formatOps.find(D3DDDIFMT_X8R8G8B8);
		if (it != formatOps.end() && 0 != it->second.BltMsTypes)
		{
			DDIMULTISAMPLEQUALITYLEVELSDATA levels = {};
			levels.Format = D3DDDIFMT_X8R8G8B8;
			levels.MsType = D3DDDIMULTISAMPLE_NONMASKABLE;
			getCaps(D3DDDICAPS_GETMULTISAMPLEQUALITYLEVELS, levels);

			std::ostringstream oss;
			oss << "msaa(" << levels.QualityLevels - 1 << ')';

			for (UINT i = D3DDDIMULTISAMPLE_2_SAMPLES; i <= D3DDDIMULTISAMPLE_16_SAMPLES; ++i)
			{
				if (it->second.BltMsTypes & (1 << (i - 1)))
				{
					levels.MsType = static_cast<D3DDDIMULTISAMPLE_TYPE>(i);
					levels.QualityLevels = 0;
					getCaps(D3DDDICAPS_GETMULTISAMPLEQUALITYLEVELS, levels);
					oss << ", msaa" << i << "x(" << levels.QualityLevels - 1 << ')';
				}
			}
			return oss.str();
		}
		return "none";
	}

	DWORD Adapter::getSupportedZBufferBitDepths(const std::map<D3DDDIFORMAT, FORMATOP>& formatOps) const
	{
		DWORD supportedZBufferBitDepths = 0;
		if (formatOps.find(D3DDDIFMT_D16) != formatOps.end())
		{
			supportedZBufferBitDepths |= DDBD_16;
		}
		if (formatOps.find(D3DDDIFMT_X8D24) != formatOps.end())
		{
			supportedZBufferBitDepths |= DDBD_24;
		}
		if (formatOps.find(D3DDDIFMT_D32) != formatOps.end())
		{
			supportedZBufferBitDepths |= DDBD_32;
		}
		return supportedZBufferBitDepths;
	}

	HRESULT Adapter::pfnCloseAdapter()
	{
		auto adapter = m_adapter;
		auto pfnCloseAdapter = m_origVtable.pfnCloseAdapter;
		s_adapters.erase(adapter);
		return pfnCloseAdapter(adapter);
	}

	HRESULT Adapter::pfnCreateDevice(D3DDDIARG_CREATEDEVICE* pCreateData)
	{
		DeviceCallbacks::hookVtable(*pCreateData->pCallbacks, m_runtimeVersion);
		HRESULT result = m_origVtable.pfnCreateDevice(m_adapter, pCreateData);
		if (SUCCEEDED(result))
		{
			DeviceFuncs::hookVtable(*pCreateData->pDeviceFuncs, m_driverVersion);
			Device::add(*this, pCreateData->hDevice);
		}
		return result;
	}

	HRESULT Adapter::pfnGetCaps(const D3DDDIARG_GETCAPS* pData)
	{
		HRESULT result = m_origVtable.pfnGetCaps(m_adapter, pData);
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
				caps.dwDeviceZBufferBitDepth = getInfo().supportedZBufferBitDepths;
			}
			break;
		}
		}

		return result;
	}

	void Adapter::setRepository(LUID luid, const DDraw::DirectDraw::Repository& repository)
	{
		for (auto& adapter : s_adapters)
		{
			if (adapter.second.m_luid == luid)
			{
				adapter.second.m_repository = repository;
			}
		}
	}

	std::map<HANDLE, Adapter> Adapter::s_adapters;
	std::map<LUID, Adapter::AdapterInfo> Adapter::s_adapterInfos;
}
