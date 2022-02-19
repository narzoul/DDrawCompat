#pragma once

#include <vector>

#include <Config/Setting.h>
#include <Input/HotKey.h>

namespace Config
{
	class HotKeySetting : public Setting
	{
	public:
		const Input::HotKey& get() const { return m_value; }
		virtual std::string getValueStr() const override { return toString(m_value); }

	protected:
		HotKeySetting(const std::string& name, const std::string& default) : Setting(name, default) {}

		virtual void setValue(const std::string& value) override { m_value = Input::parseHotKey(value); }

	private:
		Input::HotKey m_value;
	};
}
