#pragma once

#include <map>
#include <set>
#include <vector>

#include <d3d.h>
#include <d3dumddi.h>

#include <Config/ListSetting.h>

namespace Config
{
	class FormatListSetting : public ListSetting
	{
	public:
		bool isSupported(D3DDDIFORMAT format) const;

	protected:
		FormatListSetting(const std::string& name, const std::string& default,
			const std::set<D3DDDIFORMAT>& allowedFormats,
			const std::map<std::string, std::vector<D3DDDIFORMAT>>& allowedGroups,
			bool allowFourCCs);

		virtual std::string getValueStr() const override;
		virtual void setValues(const std::vector<std::string>& values) override;

	private:
		const std::set<D3DDDIFORMAT> m_allowedFormats;
		const std::map<std::string, std::vector<D3DDDIFORMAT>> m_allowedGroups;
		std::vector<D3DDDIFORMAT> m_formats;
		const bool m_allowFourCCs;
	};
}
