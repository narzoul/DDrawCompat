#pragma once

#include <ostream>

#include <d3d.h>
#include <d3dumddi.h>

#include <D3dDdi/Log/CommonLog.h>

std::ostream& operator<<(std::ostream& os, const D3DDDIARG_CREATEDEVICE& data);
std::ostream& operator<<(std::ostream& os, const D3DDDIARG_GETCAPS& data);
std::ostream& operator<<(std::ostream& os, D3DDDICAPS_TYPE data);
std::ostream& operator<<(std::ostream& os, const FORMATOP& data);
