#pragma once

#include <Config/Settings/AlignSysMemSurfaces.h>
#include <Config/Settings/AlternatePixelCenter.h>
#include <Config/Settings/AltTabFix.h>
#include <Config/Settings/Antialiasing.h>
#include <Config/Settings/BltFilter.h>
#include <Config/Settings/ConfigHotKey.h>
#include <Config/Settings/CpuAffinity.h>
#include <Config/Settings/DesktopColorDepth.h>
#include <Config/Settings/DisplayFilter.h>
#include <Config/Settings/DisplayRefreshRate.h>
#include <Config/Settings/DisplayResolution.h>
#include <Config/Settings/DpiAwareness.h>
#include <Config/Settings/ForceD3D9On12.h>
#include <Config/Settings/FpsLimiter.h>
#include <Config/Settings/FullscreenMode.h>
#include <Config/Settings/LogLevel.h>
#include <Config/Settings/RemoveBorders.h>
#include <Config/Settings/RenderColorDepth.h>
#include <Config/Settings/ResolutionScale.h>
#include <Config/Settings/SpriteDetection.h>
#include <Config/Settings/SpriteFilter.h>
#include <Config/Settings/SpriteTexCoord.h>
#include <Config/Settings/SupportedResolutions.h>
#include <Config/Settings/TextureFilter.h>
#include <Config/Settings/ThreadPriorityBoost.h>
#include <Config/Settings/VSync.h>
#include <Config/Settings/WinVersionLie.h>

namespace Config
{
	extern Settings::AlignSysMemSurfaces alignSysMemSurfaces;
	extern Settings::AlternatePixelCenter alternatePixelCenter;
	extern Settings::AltTabFix altTabFix;
	extern Settings::Antialiasing antialiasing;
	extern Settings::BltFilter bltFilter;
	extern Settings::ConfigHotKey configHotKey;
	extern Settings::CpuAffinity cpuAffinity;
	extern Settings::DesktopColorDepth desktopColorDepth;
	extern Settings::DisplayFilter displayFilter;
	extern Settings::DisplayRefreshRate displayRefreshRate;
	extern Settings::DisplayResolution displayResolution;
	extern Settings::DpiAwareness dpiAwareness;
	extern Settings::ForceD3D9On12 forceD3D9On12;
	extern Settings::FpsLimiter fpsLimiter;
	extern Settings::FullscreenMode fullscreenMode;
	extern Settings::LogLevel logLevel;
	extern Settings::RemoveBorders removeBorders;
	extern Settings::RenderColorDepth renderColorDepth;
	extern Settings::ResolutionScale resolutionScale;
	extern Settings::SpriteDetection spriteDetection;
	extern Settings::SpriteFilter spriteFilter;
	extern Settings::SpriteTexCoord spriteTexCoord;
	extern Settings::SupportedResolutions supportedResolutions;
	extern Settings::TextureFilter textureFilter;
	extern Settings::ThreadPriorityBoost threadPriorityBoost;
	extern Settings::VSync vSync;
	extern Settings::WinVersionLie winVersionLie;
}
