#pragma once

#include <Windows.h>

namespace Gdi
{
	namespace DcCache
	{
		void deleteDc(HDC cachedDc);
		void dllProcessDetach();
		void dllThreadDetach();
		HDC getDc();
		void releaseDc(HDC cachedDc);
	}
}
