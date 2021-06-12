#pragma once

#include <string>

namespace Config
{
	class Setting
	{
	public:
		Setting(const std::string& name);

		Setting(const Setting&) = delete;
		Setting(Setting&&) = delete;
		Setting& operator=(const Setting&) = delete;
		Setting& operator=(Setting&&) = delete;

		const std::string& getName() const { return m_name; }
		const std::string& getSource() const { return m_source; }
		std::string getValueAsString() const { return getValueStr(); }

		void reset();
		void set(const std::string& value, const std::string& source);

	protected:
		virtual std::string getValueStr() const = 0;
		virtual void resetValue() = 0;
		virtual void setValue(const std::string& value) = 0;

	private:
		std::string m_name;
		std::string m_source;
	};
}
