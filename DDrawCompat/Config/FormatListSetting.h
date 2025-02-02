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

	protected:
		FormatListSetting(const std::string& name, const std::string& default,
			const std::set<D3DDDIFORMAT>& allowedFormats,
			const std::map<std::string, std::set<D3DDDIFORMAT>>& allowedGroups,
			bool allowFourCCs);

	private:
		virtual std::string addValue(const std::string& value) override;
		virtual void clear() override;

		const std::set<D3DDDIFORMAT> m_allowedFormats;
		const std::map<std::string, std::set<D3DDDIFORMAT>> m_allowedGroups;
		const bool m_allowFourCCs;

		std::set<D3DDDIFORMAT> m_formats;
		bool m_allowAll;
	};
}
