#pragma once

#include <Windows.h>

namespace Gdi
{
	namespace Dc
	{
		void dllProcessDetach();
		void dllThreadDetach();
		HDC getDc(HDC origDc, bool useMetaRgn = true);
		HDC getOrigDc(HDC dc);
		void releaseDc(HDC origDc);
	}
}
