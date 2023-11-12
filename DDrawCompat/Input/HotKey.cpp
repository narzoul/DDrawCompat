#include <map>

#include <Config/Parser.h>
#include <Input/HotKey.h>
#include <Input/Input.h>

namespace
{
	const std::map<std::string, UINT> g_keyNames = []() {
		std::map<std::string, UINT> names;
#define VK_HANGUEL 0x15
#define ADD_KEY_NAME(key) names[Config::Parser::tolower(std::string(#key).substr(3))] = key;
		ADD_KEY_NAME(VK_CANCEL);
		ADD_KEY_NAME(VK_BACK);
		ADD_KEY_NAME(VK_TAB);
		ADD_KEY_NAME(VK_CLEAR);
		ADD_KEY_NAME(VK_RETURN);
		ADD_KEY_NAME(VK_SHIFT);
		ADD_KEY_NAME(VK_CONTROL);
		ADD_KEY_NAME(VK_MENU);
		ADD_KEY_NAME(VK_PAUSE);
		ADD_KEY_NAME(VK_CAPITAL);
		ADD_KEY_NAME(VK_KANA);
		ADD_KEY_NAME(VK_HANGEUL);
		ADD_KEY_NAME(VK_HANGUEL);
		ADD_KEY_NAME(VK_HANGUL);
		ADD_KEY_NAME(VK_IME_ON);
		ADD_KEY_NAME(VK_JUNJA);
		ADD_KEY_NAME(VK_FINAL);
		ADD_KEY_NAME(VK_HANJA);
		ADD_KEY_NAME(VK_KANJI);
		ADD_KEY_NAME(VK_IME_OFF);
		ADD_KEY_NAME(VK_ESCAPE);
		ADD_KEY_NAME(VK_CONVERT);
		ADD_KEY_NAME(VK_NONCONVERT);
		ADD_KEY_NAME(VK_ACCEPT);
		ADD_KEY_NAME(VK_MODECHANGE);
		ADD_KEY_NAME(VK_SPACE);
		ADD_KEY_NAME(VK_PRIOR);
		ADD_KEY_NAME(VK_NEXT);
		ADD_KEY_NAME(VK_END);
		ADD_KEY_NAME(VK_HOME);
		ADD_KEY_NAME(VK_LEFT);
		ADD_KEY_NAME(VK_UP);
		ADD_KEY_NAME(VK_RIGHT);
		ADD_KEY_NAME(VK_DOWN);
		ADD_KEY_NAME(VK_SELECT);
		ADD_KEY_NAME(VK_PRINT);
		ADD_KEY_NAME(VK_EXECUTE);
		ADD_KEY_NAME(VK_SNAPSHOT);
		ADD_KEY_NAME(VK_INSERT);
		ADD_KEY_NAME(VK_DELETE);
		ADD_KEY_NAME(VK_HELP);
		ADD_KEY_NAME(VK_LWIN);
		ADD_KEY_NAME(VK_RWIN);
		ADD_KEY_NAME(VK_APPS);
		ADD_KEY_NAME(VK_SLEEP);
		ADD_KEY_NAME(VK_NUMPAD0);
		ADD_KEY_NAME(VK_NUMPAD1);
		ADD_KEY_NAME(VK_NUMPAD2);
		ADD_KEY_NAME(VK_NUMPAD3);
		ADD_KEY_NAME(VK_NUMPAD4);
		ADD_KEY_NAME(VK_NUMPAD5);
		ADD_KEY_NAME(VK_NUMPAD6);
		ADD_KEY_NAME(VK_NUMPAD7);
		ADD_KEY_NAME(VK_NUMPAD8);
		ADD_KEY_NAME(VK_NUMPAD9);
		ADD_KEY_NAME(VK_MULTIPLY);
		ADD_KEY_NAME(VK_ADD);
		ADD_KEY_NAME(VK_SEPARATOR);
		ADD_KEY_NAME(VK_SUBTRACT);
		ADD_KEY_NAME(VK_DECIMAL);
		ADD_KEY_NAME(VK_DIVIDE);
		ADD_KEY_NAME(VK_F1);
		ADD_KEY_NAME(VK_F2);
		ADD_KEY_NAME(VK_F3);
		ADD_KEY_NAME(VK_F4);
		ADD_KEY_NAME(VK_F5);
		ADD_KEY_NAME(VK_F6);
		ADD_KEY_NAME(VK_F7);
		ADD_KEY_NAME(VK_F8);
		ADD_KEY_NAME(VK_F9);
		ADD_KEY_NAME(VK_F10);
		ADD_KEY_NAME(VK_F11);
		ADD_KEY_NAME(VK_F12);
		ADD_KEY_NAME(VK_F13);
		ADD_KEY_NAME(VK_F14);
		ADD_KEY_NAME(VK_F15);
		ADD_KEY_NAME(VK_F16);
		ADD_KEY_NAME(VK_F17);
		ADD_KEY_NAME(VK_F18);
		ADD_KEY_NAME(VK_F19);
		ADD_KEY_NAME(VK_F20);
		ADD_KEY_NAME(VK_F21);
		ADD_KEY_NAME(VK_F22);
		ADD_KEY_NAME(VK_F23);
		ADD_KEY_NAME(VK_F24);
		ADD_KEY_NAME(VK_NUMLOCK);
		ADD_KEY_NAME(VK_SCROLL);
		ADD_KEY_NAME(VK_LSHIFT);
		ADD_KEY_NAME(VK_RSHIFT);
		ADD_KEY_NAME(VK_LCONTROL);
		ADD_KEY_NAME(VK_RCONTROL);
		ADD_KEY_NAME(VK_LMENU);
		ADD_KEY_NAME(VK_RMENU);
		ADD_KEY_NAME(VK_BROWSER_BACK);
		ADD_KEY_NAME(VK_BROWSER_FORWARD);
		ADD_KEY_NAME(VK_BROWSER_REFRESH);
		ADD_KEY_NAME(VK_BROWSER_STOP);
		ADD_KEY_NAME(VK_BROWSER_SEARCH);
		ADD_KEY_NAME(VK_BROWSER_FAVORITES);
		ADD_KEY_NAME(VK_BROWSER_HOME);
		ADD_KEY_NAME(VK_VOLUME_MUTE);
		ADD_KEY_NAME(VK_VOLUME_DOWN);
		ADD_KEY_NAME(VK_VOLUME_UP);
		ADD_KEY_NAME(VK_MEDIA_NEXT_TRACK);
		ADD_KEY_NAME(VK_MEDIA_PREV_TRACK);
		ADD_KEY_NAME(VK_MEDIA_STOP);
		ADD_KEY_NAME(VK_MEDIA_PLAY_PAUSE);
		ADD_KEY_NAME(VK_LAUNCH_MAIL);
		ADD_KEY_NAME(VK_LAUNCH_MEDIA_SELECT);
		ADD_KEY_NAME(VK_LAUNCH_APP1);
		ADD_KEY_NAME(VK_LAUNCH_APP2);
		ADD_KEY_NAME(VK_OEM_1);
		ADD_KEY_NAME(VK_OEM_PLUS);
		ADD_KEY_NAME(VK_OEM_COMMA);
		ADD_KEY_NAME(VK_OEM_MINUS);
		ADD_KEY_NAME(VK_OEM_PERIOD);
		ADD_KEY_NAME(VK_OEM_2);
		ADD_KEY_NAME(VK_OEM_3);
		ADD_KEY_NAME(VK_OEM_4);
		ADD_KEY_NAME(VK_OEM_5);
		ADD_KEY_NAME(VK_OEM_6);
		ADD_KEY_NAME(VK_OEM_7);
		ADD_KEY_NAME(VK_OEM_8);
		ADD_KEY_NAME(VK_OEM_102);
		ADD_KEY_NAME(VK_PROCESSKEY);
		ADD_KEY_NAME(VK_PACKET);
		ADD_KEY_NAME(VK_ATTN);
		ADD_KEY_NAME(VK_CRSEL);
		ADD_KEY_NAME(VK_EXSEL);
		ADD_KEY_NAME(VK_EREOF);
		ADD_KEY_NAME(VK_PLAY);
		ADD_KEY_NAME(VK_ZOOM);
		ADD_KEY_NAME(VK_NONAME);
		ADD_KEY_NAME(VK_PA1);
		ADD_KEY_NAME(VK_OEM_CLEAR);
#undef ADD_KEY_NAME
		return names;
	}();

	const std::map<std::string, UINT> g_altKeyNames = {
		{ "alt", VK_MENU },
		{ "lalt", VK_LMENU },
		{ "ralt", VK_RMENU },
		{ "ctrl", VK_CONTROL },
		{ "lctrl", VK_LCONTROL },
		{ "rctrl", VK_RCONTROL }
	};

	bool areExcludedModifierKeysDown(const std::set<UINT>& modifiers, UINT either, UINT left, UINT right)
	{
		return modifiers.find(either) == modifiers.end() &&
			modifiers.find(left) == modifiers.end() &&
			modifiers.find(right) == modifiers.end() &&
			Input::isKeyDown(either);
	}

	UINT getKeyCode(const std::string& name, const std::map<std::string, UINT>& keyNames)
	{
		auto it = keyNames.find(name);
		return it != keyNames.end() ? it->second : 0;
	}

	UINT getKeyCode(const std::string& name)
	{
		if (1 == name.length())
		{
			if (name[0] >= '0' && name[0] <= '9' ||
				name[0] >= 'a' && name[0] <= 'z')
			{
				return std::toupper(name[0], std::locale());
			}
		}

		auto code = getKeyCode(name, g_altKeyNames);
		if (0 == code)
		{
			code = getKeyCode(name, g_keyNames);
			if (0 == code)
			{
				throw Config::ParsingError("Invalid hotkey: '" + name + "'");
			}
		}
		return code;
	}

	std::string getKeyName(UINT key, const std::map<std::string, UINT>& keyNames)
	{
		auto it = std::find_if(keyNames.begin(), keyNames.end(), [&](const auto& pair) { return pair.second == key; });
		return it != keyNames.end() ? it->first : std::string();
	}

	std::string getKeyName(UINT key)
	{
		if (key >= '0' && key <= '9' ||
			key >= 'A' && key <= 'Z')
		{
			return std::string(1, static_cast<char>(std::tolower(key, std::locale())));
		}

		auto name(getKeyName(key, g_altKeyNames));
		if (name.empty())
		{
			name = getKeyName(key, g_keyNames);
		}
		return name.empty() ? "none" : name;
	}
}

namespace Input
{
	bool areModifierKeysDown(const std::set<UINT>& modifiers)
	{
		for (auto modifier : modifiers)
		{
			if (!isKeyDown(modifier))
			{
				return false;
			}
		}

		return !areExcludedModifierKeysDown(modifiers, VK_SHIFT, VK_LSHIFT, VK_RSHIFT) &&
			!areExcludedModifierKeysDown(modifiers, VK_CONTROL, VK_LCONTROL, VK_RCONTROL) &&
			!areExcludedModifierKeysDown(modifiers, VK_MENU, VK_LMENU, VK_RMENU);
	}

	bool isModifierKey(UINT vk)
	{
		return vk >= VK_SHIFT && vk <= VK_MENU ||
			vk >= VK_LSHIFT && vk <= VK_RMENU;
	}

	HotKey parseHotKey(std::string str)
	{
		HotKey hotKey;
		if (str.empty() || "none" == str)
		{
			return hotKey;
		}

		int pos = str.find('+');
		while (std::string::npos != pos)
		{
			std::string modifierName(Config::Parser::trim(str.substr(0, pos)));
			UINT modifier = getKeyCode(modifierName);
			if (!isModifierKey(modifier))
			{
				throw Config::ParsingError("Not a modifier key: '" + modifierName + "'");
			}
			hotKey.modifiers.insert(modifier);
			str = str.substr(pos + 1);
			pos = str.find('+');
		}

		hotKey.vk = getKeyCode(str);
		if (isModifierKey(hotKey.vk))
		{
			throw Config::ParsingError("A hotkey cannot end with a modifier key: '" + str + "'");
		}
		return hotKey;
	}

	std::string toString(const HotKey& hotKey)
	{
		if (!hotKey.vk)
		{
			return "none";
		}

		std::string str;
		for (UINT modifier : hotKey.modifiers)
		{
			str += getKeyName(modifier) + '+';
		}
		return str + getKeyName(hotKey.vk);
	}
}
