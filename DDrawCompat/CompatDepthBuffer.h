#pragma once

#include <guiddef.h>

#include "CompatPtr.h"

namespace CompatDepthBuffer
{
	template <typename TDirect3d, typename TD3dDeviceDesc>
	void fixSupportedZBufferBitDepths(CompatPtr<TDirect3d> d3d, TD3dDeviceDesc& desc);
}
