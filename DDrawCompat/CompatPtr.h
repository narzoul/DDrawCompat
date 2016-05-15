#pragma once

#include <algorithm>

#include "CompatQueryInterface.h"
#include "CompatWeakPtr.h"

template <typename Intf>
class CompatPtr : public CompatWeakPtr<Intf>
{
public:
	CompatPtr(std::nullptr_t = nullptr)
	{
	}

	explicit CompatPtr(Intf* intf) : CompatWeakPtr(intf)
	{
	}

	CompatPtr(const CompatPtr& other)
	{
		m_intf = Compat::queryInterface<Intf>(other.get());
	}

	template <typename OtherIntf>
	CompatPtr(const CompatPtr<OtherIntf>& other)
	{
		m_intf = Compat::queryInterface<Intf>(other.get());
	}

	~CompatPtr()
	{
		release();
	}

	CompatPtr& operator=(CompatPtr rhs)
	{
		swap(rhs);
		return *this;
	}

	Intf* detach()
	{
		Intf* intf = m_intf;
		m_intf = nullptr;
		return intf;
	}

	void reset(Intf* intf = nullptr)
	{
		*this = CompatPtr(intf);
	}

	void swap(CompatPtr& other)
	{
		std::swap(m_intf, other.m_intf);
	}
};
