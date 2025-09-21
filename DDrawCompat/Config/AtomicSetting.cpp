#include <Config/AtomicSetting.h>

namespace Config
{
	AtomicSettingStore::AtomicSettingStore(MappedSetting<unsigned>& setting)
		: m_setting(setting)
	{
		update();
	}

	AtomicSetting AtomicSettingStore::get() const
	{
		const auto store = m_store.load(std::memory_order_relaxed);
		return { store >> 16, store & 0xFFFF };
	}

	void AtomicSettingStore::update()
	{
		m_store.store(m_setting.get() << 16 | m_setting.getParam(), std::memory_order_relaxed);
	}
}
