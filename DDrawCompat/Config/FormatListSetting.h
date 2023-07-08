#pragma once

#include <map>
#include <set>

#include <d3d.h>
#include <d3dumddi.h>

#include <Config/ListSetting.h>

namespace Config
{
	class FormatListSetting : public ListSetting
	{
	public:
		bool isSupported(D3DDDIFORMAT format) const;

		virtual std::string getValueStr() const override;

	protected:
		FormatListSetting(const std::string& name, const std::string& default,
			const std::set<D3DDDIFORMAT>& allowedFormats,
			const std::map<std::string, std::set<D3DDDIFORMAT>>& allowedGroups,
			bool allowFourCCs);

		virtual void setValues(const std::vector<std::string>& values) override;

	private:
		const std::set<D3DDDIFORMAT> m_allowedFormats;
		const std::map<std::string, std::set<D3DDDIFORMAT>> m_allowedGroups;
		std::set<D3DDDIFORMAT> m_formats;
		std::string m_valueStr;
		const bool m_allowFourCCs;
	};
}
