#include <array>
#include <map>
#include <sstream>

#include <Common/Comparison.h>
#include <Common/CompatVtable.h>
#include <Common/Rect.h>
#include <Config/Settings/Antialiasing.h>
#include <Config/Settings/DisplayAspectRatio.h>
#include <Config/Settings/PalettizedTextures.h>
#include <Config/Settings/RenderColorDepth.h>
#include <Config/Settings/ResolutionScale.h>
#include <Config/Settings/SupportedDepthFormats.h>
#include <D3dDdi/Adapter.h>
#include <D3dDdi/AdapterFuncs.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/DeviceCallbacks.h>
#include <D3dDdi/DeviceFuncs.h>
#include <D3dDdi/FormatInfo.h>
#include <D3dDdi/KernelModeThunks.h>
#include <D3dDdi/SurfaceRepository.h>
#include <Win32/DisplayMode.h>

namespace
{
	struct DepthFormat
	{
		DWORD flag;
		D3DDDIFORMAT format;
	};

	const std::array<DepthFormat, 3> g_depthFormats = { {
		{ DDBD_16, D3DDDIFMT_D16 },
		{ DDBD_24, D3DDDIFMT_X8D24 },
		{ DDBD_32, D3DDDIFMT_D32 }
	} };

	std::string bitDepthsToString(DWORD bitDepths)
	{
		std::string result;
		if (bitDepths & DDBD_8) { result += ", 8"; }
		if (bitDepths & DDBD_16) { result += ", 16"; }
		if (bitDepths & DDBD_24) { result += ", 24"; }
		if (bitDepths & DDBD_32) { result += ", 32"; }

		if (result.empty())
		{
			return "none";
		}
		return result.substr(2);
	}
}

namespace D3dDdi
{
	Adapter::Adapter(const D3DDDIARG_OPENADAPTER& data)
		: m_adapter(data.hAdapter)
		, m_origVtable(CompatVtable<D3DDDI_ADAPTERFUNCS>::s_origVtable)
		, m_runtimeVersion(data.Version)
		, m_driverVersion(data.DriverVersion)
		, m_guid(nullptr)
		, m_guidBuf{}
		, m_luid(KernelModeThunks::getLastOpenAdapterInfo().luid)
		, m_deviceName(KernelModeThunks::getLastOpenAdapterInfo().deviceName)
		, m_repository{}
		, m_info(findInfo())
	{
	}

	SIZE Adapter::getAspectRatio(SIZE appRes, SIZE displayRes) const
	{
		SIZE ar = Config::displayAspectRatio.get();
		if (Config::Settings::DisplayAspectRatio::APP == ar)
		{
			return 0 != appRes.cx ? appRes : Rect::getSize(Win32::DisplayMode::getMonitorInfo(m_deviceName).rcEmulated);
		}
		else if (Config::Settings::DisplayAspectRatio::DISPLAY == ar)
		{
			return 0 != displayRes.cx ? displayRes : Rect::getSize(Win32::DisplayMode::getMonitorInfo(m_deviceName).rcReal);
		}
		return ar;
	}

	SIZE Adapter::getAspectRatio() const
	{
		return getAspectRatio({}, {});
	}

	const Adapter::AdapterInfo& Adapter::findInfo() const
	{
		auto it = s_adapterInfos.find(m_luid);
		if (it != s_adapterInfos.end())
		{
			return it->second;
		}

		AdapterInfo& info = s_adapterInfos.insert({ m_luid, {} }).first->second;
		getCaps(D3DDDICAPS_GETD3D7CAPS, info.d3dExtendedCaps);

		LOG_INFO << "Supported resource formats:";
		info.formatOps = getFormatOps();

		auto d3d9on12 = GetModuleHandle("d3d9on12");
		info.isD3D9On12 = d3d9on12 && d3d9on12 == Compat::getModuleHandleFromAddress(m_origVtable.pfnGetCaps);

		auto trinity = GetModuleHandle("igd9trinity32");
		info.isTrinity = trinity && trinity == Compat::getModuleHandleFromAddress(m_origVtable.pfnGetCaps);

		info.isMsaaDepthResolveSupported =
			!info.isD3D9On12 &&
			info.formatOps.find(FOURCC_RESZ) != info.formatOps.end() &&
			info.formatOps.find(FOURCC_INTZ) != info.formatOps.end() &&
			info.formatOps.find(FOURCC_NULL) != info.formatOps.end();
		info.fixedFormatOps = getFixedFormatOps(info);
		info.supportedZBufferBitDepths = getSupportedZBufferBitDepths(info.fixedFormatOps);

		LOG_INFO << "Supported z-buffer bit depths: " << bitDepthsToString(info.supportedZBufferBitDepths);
		LOG_INFO << "Supported MSAA modes: " << getSupportedMsaaModes(info.formatOps);

		for (const auto& depthFormat : g_depthFormats)
		{
			if (!Config::supportedDepthFormats.isSupported(depthFormat.format))
			{
				info.supportedZBufferBitDepths &= ~depthFormat.flag;
			}
		}

		return info;
	}

	std::map<D3DDDIFORMAT, FORMATOP> Adapter::getFixedFormatOps(const AdapterInfo& info) const
	{
		std::map<D3DDDIFORMAT, FORMATOP> fixedFormatOps;

		for (auto& formatOp : info.formatOps)
		{
			if (D3DDDIFMT_P8 == formatOp.first)
			{
				continue;
			}

			auto fixedFormatOp = formatOp.second;
			if (isEmulatedRenderTargetFormat(formatOp.first, info.formatOps))
			{
				fixedFormatOp.Operations |= FORMATOP_OFFSCREEN_RENDERTARGET;
			}

			if (D3DDDIFMT_L8 == formatOp.first)
			{
				auto p8FormatOp = formatOp.second;
				p8FormatOp.Format = D3DDDIFMT_P8;
				p8FormatOp.Operations |= FORMATOP_OFFSCREENPLAIN;
				fixedFormatOps[D3DDDIFMT_P8] = p8FormatOp;
			}

			if (D3DDDIFMT_D24X4S4 == formatOp.first || D3DDDIFMT_X4S4D24 == formatOp.first)
			{
				// If these formats are reported as depth buffers, then EnumZBufferFormats returns only D16
				fixedFormatOp.Operations &= ~(FORMATOP_ZSTENCIL | FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH);
			}

			if (info.isD3D9On12)
			{
				if (D3DDDIFMT_D24X8 == formatOp.first)
				{
					fixedFormatOp.Format = D3DDDIFMT_X8D24;
				}
				else if (D3DDDIFMT_D24S8 == formatOp.first)
				{
					fixedFormatOp.Format = D3DDDIFMT_S8D24;
				}
			}

			fixedFormatOps[fixedFormatOp.Format] = fixedFormatOp;
		}

		return fixedFormatOps;
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
			if (D3DDDIFMT_UNKNOWN != formatOps[i].Format)
			{
				LOG_INFO << "  " << formatOps[i];
				auto& formatOp = result[formatOps[i].Format];
				formatOp.Format = formatOps[i].Format;
				formatOp.Operations |= formatOps[i].Operations;
				formatOp.FlipMsTypes |= formatOps[i].FlipMsTypes;
				formatOp.BltMsTypes |= formatOps[i].BltMsTypes;
				if (formatOps[i].PrivateFormatBitCount > formatOp.PrivateFormatBitCount)
				{
					formatOp.PrivateFormatBitCount = formatOps[i].PrivateFormatBitCount;
				}
			}
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
		if (it == info.formatOps.end())
		{
			return { D3DDDIMULTISAMPLE_NONE, 0 };
		}

		auto msTypes = it->second.BltMsTypes;
		if (0 == msTypes && (FOURCC_DF16 == it->first || FOURCC_INTZ == it->first))
		{
			auto replacement = info.formatOps.find(FOURCC_DF16 == it->first ? D3DDDIFMT_D16 : D3DDDIFMT_S8D24);
			if (replacement != info.formatOps.end())
			{
				msTypes = replacement->second.BltMsTypes;
			}
		}

		while (samples > 0 && !(msTypes & (1 << (samples - 1))))
		{
			--samples;
		}

		if (0 == samples)
		{
			return { D3DDDIMULTISAMPLE_NONE, 0 };
		}

		DDIMULTISAMPLEQUALITYLEVELSDATA levels = {};
		levels.Format = D3DDDIFMT_X8R8G8B8;
		levels.MsType = static_cast<D3DDDIMULTISAMPLE_TYPE>(samples);
		getCaps(D3DDDICAPS_GETMULTISAMPLEQUALITYLEVELS, levels);
		return { levels.MsType, std::min(static_cast<UINT>(Config::antialiasing.getParam()), levels.QualityLevels - 1) };
	}

	D3DDDIFORMAT Adapter::getRenderColorDepthSrcFormat(D3DDDIFORMAT appFormat) const
	{
		switch (Config::renderColorDepth.get().first)
		{
		case 10:
			if (isSupportedRttFormat(D3DDDIFMT_A2B10G10R10))
			{
				return D3DDDIFMT_A2B10G10R10;
			}
			if (isSupportedRttFormat(D3DDDIFMT_A2R10G10B10))
			{
				return D3DDDIFMT_A2R10G10B10;
			}
			[[fallthrough]];

		case 8:
			return D3DDDIFMT_X8R8G8B8;
			
		case 6:
			return D3DDDIFMT_R5G6B5;
		}

		return appFormat;
	}

	D3DDDIFORMAT Adapter::getRenderColorDepthDstFormat() const
	{
		switch (Config::renderColorDepth.get().second)
		{
		case 6: return D3DDDIFMT_R5G6B5;
		case 8: return D3DDDIFMT_X8R8G8B8;
		}
		return 16 == Win32::DisplayMode::getBpp() ? D3DDDIFMT_R5G6B5 : D3DDDIFMT_X8R8G8B8;
	}

	SIZE Adapter::getScaledSize(Int2 size) const
	{
		const auto scaleFactor = getScaleFactor();
		const int multiplier = Config::resolutionScale.getParam();
		const auto& caps = getInfo().d3dExtendedCaps;

		Int2 maxSize = { caps.dwMaxTextureWidth, caps.dwMaxTextureHeight };
		if (multiplier < 0)
		{
			maxSize = maxSize / size * size;
		}

		Int2 scaledSize = Float2(size) * scaleFactor;
		scaledSize = min(scaledSize, maxSize);
		scaledSize = max(scaledSize, size);
		return { scaledSize.x, scaledSize.y };
	}

	Float2 Adapter::getScaleFactor() const
	{
		const SIZE resolutionScale = Config::resolutionScale.get();
		const int multiplier = Config::resolutionScale.getParam();
		if (0 == multiplier ||
			Config::Settings::ResolutionScale::APP == resolutionScale && 1 == multiplier)
		{
			return 1;
		}

		const auto& mi = Win32::DisplayMode::getMonitorInfo(m_deviceName);
		auto displayRes = Rect::getSize(mi.rcReal);
		auto appRes = Rect::getSize(mi.isEmulated ? mi.rcEmulated : mi.rcReal);

		Int2 targetResolution = resolutionScale;
		if (Config::Settings::ResolutionScale::APP == resolutionScale)
		{
			targetResolution = appRes;
		}
		else if (Config::Settings::ResolutionScale::DISPLAY == resolutionScale)
		{
			targetResolution = displayRes;
		}

		targetResolution *= abs(multiplier);

		const SIZE ar = getAspectRatio(appRes, displayRes);
		if (targetResolution.y * ar.cx / ar.cy <= targetResolution.x)
		{
			targetResolution.x = targetResolution.y * ar.cx / ar.cy;
		}
		else
		{
			targetResolution.y = targetResolution.x * ar.cy / ar.cx;
		}

		const auto scaleFactor = Float2(targetResolution) / Float2(appRes);
		return multiplier < 0 ? scaleFactor : ceil(scaleFactor);
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
		for (const auto& depthFormat : g_depthFormats)
		{
			if (formatOps.find(depthFormat.format) != formatOps.end())
			{
				supportedZBufferBitDepths |= depthFormat.flag;
			}
		}
		return supportedZBufferBitDepths;
	}

	bool Adapter::isEmulatedRenderTargetFormat(D3DDDIFORMAT format) const
	{
		return isEmulatedRenderTargetFormat(format, m_info.formatOps);
	}

	bool Adapter::isEmulatedRenderTargetFormat(D3DDDIFORMAT format, const std::map<D3DDDIFORMAT, FORMATOP>& formatOps) const
	{
		const auto& fi = getFormatInfo(format);
		if (0 == fi.red.bitCount)
		{
			return false;
		}

		auto it = formatOps.find(format);
		if (it == formatOps.end() || (it->second.Operations & FORMATOP_OFFSCREEN_RENDERTARGET))
		{
			return false;
		}

		auto replacementFormat = 0 != fi.alpha.bitCount ? D3DDDIFMT_A8R8G8B8 : D3DDDIFMT_X8R8G8B8;
		it = formatOps.find(replacementFormat);
		return it != formatOps.end() && (it->second.Operations & FORMATOP_OFFSCREEN_RENDERTARGET);
	}

	bool Adapter::isSupportedRttFormat(D3DDDIFORMAT format) const
	{
		auto it = m_info.formatOps.find(format);
		return it != m_info.formatOps.end() &&
			(it->second.Format & FORMATOP_OFFSCREEN_RENDERTARGET) &&
			(it->second.Format & FORMATOP_TEXTURE);
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
		HRESULT result = D3DDDICAPS_GETFORMATDATA == pData->Type ? S_OK : m_origVtable.pfnGetCaps(m_adapter, pData);
		if (FAILED(result))
		{
			return result;
		}

		switch (pData->Type)
		{
		case D3DDDICAPS_DDRAW:
		{
			auto& caps = *static_cast<DDRAW_CAPS*>(pData->pData);
			caps.Caps |= DDRAW_CAPS_COLORKEY | DDRAW_CAPS_BLTDEPTHFILL;
			caps.CKeyCaps = DDRAW_CKEYCAPS_SRCBLT;
			caps.FxCaps = DDRAW_FXCAPS_BLTMIRRORLEFTRIGHT | DDRAW_FXCAPS_BLTMIRRORUPDOWN;
			break;
		}

		case D3DDDICAPS_GETD3D3CAPS:
		{
			auto& caps = static_cast<D3DNTHAL_GLOBALDRIVERDATA*>(pData->pData)->hwCaps;
			if (caps.dwFlags & D3DDD_DEVICEZBUFFERBITDEPTH)
			{
				caps.dwDeviceZBufferBitDepth = getInfo().supportedZBufferBitDepths;
			}
			if (Config::palettizedTextures.get())
			{
				caps.dpcTriCaps.dwTextureCaps |= D3DPTEXTURECAPS_ALPHAPALETTE;
			}
			break;
		}

		case D3DDDICAPS_GETFORMATCOUNT:
			*static_cast<UINT*>(pData->pData) = m_info.fixedFormatOps.size();
			break;

		case D3DDDICAPS_GETFORMATDATA:
		{
			UINT count = pData->DataSize / sizeof(FORMATOP);
			if (count > m_info.fixedFormatOps.size())
			{
				count = m_info.fixedFormatOps.size();
			}

			auto formatOp = static_cast<FORMATOP*>(pData->pData);
			auto it = m_info.fixedFormatOps.begin();
			for (UINT i = 0; i < count; ++i)
			{
				formatOp[i] = it->second;
				++it;
			}
			break;
		}
		}

		return result;
	}

	void Adapter::setRepository(LUID luid, GUID* guid, CompatWeakPtr<IDirectDraw7> repository)
	{
		for (auto& adapter : s_adapters)
		{
			if (adapter.second.m_luid == luid)
			{
				if (guid)
				{
					adapter.second.m_guidBuf = *guid;
					adapter.second.m_guid = &adapter.second.m_guidBuf;
				}
				adapter.second.m_repository = repository;
				auto& surfaceRepo = SurfaceRepository::get(adapter.second);
				surfaceRepo.setRepository(repository);
			}
		}
	}

	std::map<HANDLE, Adapter> Adapter::s_adapters;
	std::map<LUID, Adapter::AdapterInfo> Adapter::s_adapterInfos;
}
