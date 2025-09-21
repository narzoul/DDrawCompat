#pragma once

#include <atomic>

#include <Config/MappedSetting.h>

namespace Config
{
	struct AtomicSetting
	{
		unsigned value;
		unsigned param;
	};

	class AtomicSettingStore
	{
	public:
		AtomicSettingStore(MappedSetting<unsigned>& setting);

		AtomicSetting get() const;
		void update();

	private:
		MappedSetting<unsigned>& m_setting;
		std::atomic<unsigned> m_store;
	};
}
