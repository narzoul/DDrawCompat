#pragma once

#include <set>

#include <Config/ListSetting.h>

namespace Config
{
	namespace Settings
	{
		class SupportedRefreshRates : public ListSetting
		{
		public:
			static const unsigned DESKTOP = UINT_MAX;

			SupportedRefreshRates();

			bool allowAll() const { return m_allowAll; }
			std::set<unsigned> get() const { return m_refreshRates; }

		private:
			virtual std::string addValue(const std::string& value) override;
			virtual void clear() override;

			std::set<unsigned> m_refreshRates;
			bool m_allowAll;
		};
	}

	extern Settings::SupportedRefreshRates supportedRefreshRates;
}
