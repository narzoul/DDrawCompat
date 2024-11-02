#pragma once

#include <functional>

#include <Windows.h>

namespace Overlay
{
	class Control;
	class Window;
}

namespace Win32
{
	namespace DisplayMode
	{
		struct MonitorInfo;
	}
}

namespace Input
{
	struct HotKey;

	bool operator<(const HotKey& lhs, const HotKey& rhs);

	Overlay::Control* getCapture();
	Overlay::Window* getCaptureWindow();
	HWND getCursorWindow();
	POINT getRelativeCursorPos();
	void init();
	void installHooks();
	bool isKeyDown(int vk);
	void registerHotKey(const HotKey& hotKey, std::function<void(void*)> action, void* context, bool onKeydown = true);
	void setCapture(Overlay::Control* control);
	void setFullscreenMonitorInfo(const Win32::DisplayMode::MonitorInfo& mi);
	void updateCursor();
	void updateMouseSensitivity();
}
