#pragma once

#include <set>
#include <string>

#include <Windows.h>

namespace Input
{
	struct HotKey
	{
		UINT vk;
		std::set<UINT> modifiers;

		HotKey() : vk(0) {}
	};

	bool areModifierKeysDown(const std::set<UINT>& modifiers);
	bool isModifierKey(UINT vk);
	HotKey parseHotKey(std::string str);
	std::string toString(const HotKey& hotKey);
}
