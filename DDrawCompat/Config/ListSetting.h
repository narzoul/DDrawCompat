#pragma once

#include <vector>

#include <Config/Setting.h>

namespace Config
{
	class ListSetting : public Setting
	{
	public:
		virtual std::string getValueStr() const override;
		virtual bool isMultiValued() const override { return true; }

	protected:
		ListSetting(const std::string& name, const std::string& default);

		virtual void setValue(const std::string& value) override;

	private:
		virtual std::string addValue(const std::string& value) = 0;
		virtual void clear() = 0;

		std::string m_valueStr;
	};
}
