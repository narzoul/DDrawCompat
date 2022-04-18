#pragma once

#include <ostream>

#include <ddraw.h>

std::ostream& operator<<(std::ostream& os, const DDBLTFX& fx);
std::ostream& operator<<(std::ostream& os, const DDCOLORKEY& ck);
std::ostream& operator<<(std::ostream& os, const DDGAMMARAMP& ramp);
std::ostream& operator<<(std::ostream& os, const DDSCAPS& caps);
std::ostream& operator<<(std::ostream& os, const DDSCAPS2& caps);
std::ostream& operator<<(std::ostream& os, const DDPIXELFORMAT& pf);
std::ostream& operator<<(std::ostream& os, const DDSURFACEDESC& sd);
std::ostream& operator<<(std::ostream& os, const DDSURFACEDESC2& sd);
std::ostream& operator<<(std::ostream& os, const GUID& guid);
