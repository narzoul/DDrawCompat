#pragma once

#include <functional>
#include <set>

#include <Windows.h>

namespace Overlay
{
	class Window;
}

namespace Input
{
	struct HotKey
	{
		UINT vk;
		std::set<UINT> modifiers;
	};

	bool operator<(const HotKey& lhs, const HotKey& rhs);

	Overlay::Window* getCapture();
	HWND getCursorWindow();
	void installHooks();
	void registerHotKey(const HotKey& hotKey, std::function<void(void*)> action, void* context);
	void setCapture(Overlay::Window* window);
	void updateCursor();
}
