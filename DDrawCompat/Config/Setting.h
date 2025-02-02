#pragma once

#include <map>
#include <string>
#include <vector>

namespace Config
{
	class Setting
	{
	public:
		struct ParamInfo
		{
			std::string name;
			int min;
			int max;
			int default;
		};

		Setting(const std::string& name, const std::string& default);

		Setting(const Setting&) = delete;
		Setting(Setting&&) = delete;
		Setting& operator=(const Setting&) = delete;
		Setting& operator=(Setting&&) = delete;

		virtual std::vector<std::string> getDefaultValueStrings() { return {}; }
		virtual ParamInfo getParamInfo() const { return {}; }
		virtual std::string getValueStr() const = 0;
		virtual bool isMultiValued() const { return false; }

		const std::string& getBaseValue() const { return m_baseValue; }
		const std::string& getExportedValue() const { return m_exportedValue; }
		const std::string& getName() const { return m_name; }
		int getParam() const { return m_param; }
		const std::string& getSource() const { return m_source; }

		void reset();
		void set(const std::string& value, const std::string& source);
		void setBaseValue();
		void setExportedValue();

	protected:
		virtual void setValue(const std::string& value) = 0;

		int m_param;

	private:
		void setParam(const std::string& param);

		std::string m_name;
		std::string m_default;
		std::string m_source;
		std::string m_baseValue;
		std::string m_exportedValue;
	};

	const std::map<std::string, Setting&>& getAllSettings();
	Setting* getSetting(const std::string& name);
}
