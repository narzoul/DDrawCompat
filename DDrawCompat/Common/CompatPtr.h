#pragma once

#include <algorithm>

#include <Common/CompatQueryInterface.h>
#include <Common/CompatWeakPtr.h>

template <typename Intf>
class CompatPtr : public CompatWeakPtr<Intf>
{
public:
	template <typename OtherIntf>
	static CompatPtr from(OtherIntf* other)
	{
		return CompatPtr(Compat::queryInterface<Intf>(other));
	}

	CompatPtr(std::nullptr_t = nullptr)
	{
	}

	explicit CompatPtr(Intf* intf) : CompatWeakPtr<Intf>(intf)
	{
	}

	CompatPtr(const CompatPtr& other)
	{
		this->m_intf = Compat::queryInterface<Intf>(other.get());
	}

	template <typename OtherIntf>
	CompatPtr(const CompatPtr<OtherIntf>& other)
	{
		this->m_intf = Compat::queryInterface<Intf>(other.get());
	}

	~CompatPtr()
	{
		this->release();
	}

	CompatPtr& operator=(CompatPtr rhs)
	{
		swap(rhs);
		return *this;
	}

	Intf* detach()
	{
		Intf* intf = this->m_intf;
		this->m_intf = nullptr;
		return intf;
	}

	void reset(Intf* intf = nullptr)
	{
		*this = CompatPtr(intf);
	}

	void swap(CompatPtr& other)
	{
		std::swap(this->m_intf, other.m_intf);
	}
};

template <typename T>
std::ostream& operator<<(std::ostream& os, const CompatPtr<T>& ptr)
{
	return os << ptr.get();
}
