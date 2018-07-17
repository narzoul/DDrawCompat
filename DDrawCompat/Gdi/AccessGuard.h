#pragma once

namespace Gdi
{
	enum Access
	{
		ACCESS_READ,
		ACCESS_WRITE
	};

	enum User
	{
		USER_DDRAW,
		USER_GDI
	};

	class AccessGuard
	{
	protected:
		AccessGuard(User user, Access access, bool condition = true);
		~AccessGuard();

	private:
		User m_user;
		Access m_access;
		bool m_condition;
	};

	class DDrawAccessGuard : public AccessGuard
	{
	public:
		DDrawAccessGuard(Access access, bool condition = true);
	};

	class GdiAccessGuard : public AccessGuard
	{
	public:
		GdiAccessGuard(Access access, bool condition = true);
	};
}
