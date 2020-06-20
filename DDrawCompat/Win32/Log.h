#pragma once

#include <ostream>
#include <string>

#include <Windows.h>

std::ostream& operator<<(std::ostream& os, const CWPSTRUCT& cwrp);
std::ostream& operator<<(std::ostream& os, const CWPRETSTRUCT& cwrp);
std::ostream& operator<<(std::ostream& os, const DEVMODEA& dm);
std::ostream& operator<<(std::ostream& os, const DEVMODEW& dm);
std::ostream& operator<<(std::ostream& os, HDC dc);
std::ostream& operator<<(std::ostream& os, HRGN rgn);
std::ostream& operator<<(std::ostream& os, HWND hwnd);
std::ostream& operator<<(std::ostream& os, const MEMORYSTATUS& ms);
std::ostream& operator<<(std::ostream& os, const MSG& msg);
std::ostream& operator<<(std::ostream& os, const RECT& rect);

namespace Compat
{
	std::string logWm(UINT msg);
}
