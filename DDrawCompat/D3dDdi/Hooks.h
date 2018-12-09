#pragma once

#include <string>

namespace D3dDdi
{
	UINT getDdiVersion();
	void installHooks(HMODULE origDDrawModule);
	void onUmdFileNameQueried(const std::wstring& umdFileName);
	void uninstallHooks();
}
