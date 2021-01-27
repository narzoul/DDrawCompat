#pragma once

#include <Windows.h>

namespace Gdi
{
	namespace DcCache
	{
		void dllProcessDetach();
		void dllThreadDetach();
		HDC getDc(bool useDefaultPalette);
		void releaseDc(HDC cachedDc, bool useDefaultPalette);
	}
}
