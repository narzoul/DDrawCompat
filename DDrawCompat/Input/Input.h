#pragma once

#include <functional>

#include <Windows.h>

namespace Overlay
{
	class Control;
	class Window;
}

namespace Input
{
	struct HotKey;

	bool operator<(const HotKey& lhs, const HotKey& rhs);

	Overlay::Control* getCapture();
	Overlay::Window* getCaptureWindow();
	POINT getCursorPos();
	HWND getCursorWindow();
	void installHooks();
	void registerHotKey(const HotKey& hotKey, std::function<void(void*)> action, void* context);
	void setCapture(Overlay::Control* control);
	void updateCursor();
}
