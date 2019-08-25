#pragma once

namespace Gdi
{
	enum Access
	{
		ACCESS_READ,
		ACCESS_WRITE
	};

	class AccessGuard
	{
	public:
		AccessGuard(Access access, bool condition = true);
		~AccessGuard();

	private:
		Access m_access;
		bool m_condition;
	};
}
