#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

#include "D3dDdi/KernelModeThunks.h"
#include "DDraw/RealPrimarySurface.h"
#include "DDraw/ScopedThreadLock.h"
#include "DDraw/Surfaces/PrimarySurface.h"
#include "Gdi/AccessGuard.h"
#include "Gdi/VirtualScreen.h"

namespace
{
	struct Accessor
	{
		DWORD readAccessDepth;
		DWORD writeAccessDepth;
		bool isSynced;

		Accessor(bool isSynced) : readAccessDepth(0), writeAccessDepth(0), isSynced(isSynced) {}
	};

	Accessor g_ddrawAccessor(false);
	Accessor g_gdiAccessor(true);
	bool g_isSyncing = false;

	bool synchronize(Gdi::User user);

	void beginAccess(Gdi::User user, Gdi::Access access)
	{
		Compat::LogEnter("beginAccess", user, access);

		Accessor& accessor = Gdi::USER_DDRAW == user ? g_ddrawAccessor : g_gdiAccessor;
		DWORD& accessDepth = Gdi::ACCESS_READ == access ? accessor.readAccessDepth : accessor.writeAccessDepth;
		++accessDepth;

		accessor.isSynced = accessor.isSynced || synchronize(user);
		if (accessor.isSynced && Gdi::ACCESS_WRITE == access)
		{
			Accessor& otherAccessor = Gdi::USER_DDRAW == user ? g_gdiAccessor : g_ddrawAccessor;
			otherAccessor.isSynced = false;
		}

		Compat::LogLeave("beginAccess", user, access) << accessor.isSynced;
	}

	void endAccess(Gdi::User user, Gdi::Access access)
	{
		Compat::LogEnter("endAccess", user, access);

		Accessor& accessor = Gdi::USER_DDRAW == user ? g_ddrawAccessor : g_gdiAccessor;
		DWORD& accessDepth = Gdi::ACCESS_READ == access ? accessor.readAccessDepth : accessor.writeAccessDepth;
		--accessDepth;

		if (Gdi::USER_DDRAW == user &&
			0 == g_ddrawAccessor.readAccessDepth && 0 == g_ddrawAccessor.writeAccessDepth &&
			(0 != g_gdiAccessor.readAccessDepth || 0 != g_gdiAccessor.writeAccessDepth))
		{
			g_gdiAccessor.isSynced = g_gdiAccessor.isSynced || synchronize(Gdi::USER_GDI);
			if (g_gdiAccessor.isSynced && 0 != g_gdiAccessor.writeAccessDepth)
			{
				g_ddrawAccessor.isSynced = false;
			}
		}

		if (0 == accessDepth && Gdi::ACCESS_WRITE == access && Gdi::USER_GDI == user &&
			0 == g_ddrawAccessor.writeAccessDepth)
		{
			auto primary(DDraw::PrimarySurface::getPrimary());
			if (!primary || DDraw::PrimarySurface::isGdiSurface(primary.get()))
			{
				DDraw::RealPrimarySurface::update();
			}
		}

		Compat::LogLeave("endAccess", user, access);
	}

	bool synchronize(Gdi::User user)
	{
		auto ddrawSurface(DDraw::PrimarySurface::getGdiSurface());
		if (!ddrawSurface)
		{
			return false;
		}

		auto gdiSurface(Gdi::VirtualScreen::createSurface(D3dDdi::KernelModeThunks::getMonitorRect()));
		if (!gdiSurface)
		{
			return false;
		}

		bool result = true;
		g_isSyncing = true;
		if (Gdi::USER_DDRAW == user)
		{
			CompatPtr<IDirectDrawClipper> clipper;
			ddrawSurface->GetClipper(ddrawSurface, &clipper.getRef());
			ddrawSurface->SetClipper(ddrawSurface, nullptr);
			result = SUCCEEDED(ddrawSurface->Blt(
				ddrawSurface, nullptr, gdiSurface, nullptr, DDBLT_WAIT, nullptr));
			ddrawSurface->SetClipper(ddrawSurface, clipper);
		}
		else
		{
			result = SUCCEEDED(gdiSurface->BltFast(
				gdiSurface, 0, 0, ddrawSurface, nullptr, DDBLTFAST_WAIT));
		}
		g_isSyncing = false;

		return result;
	}
}

namespace Gdi
{
	AccessGuard::AccessGuard(User user, Access access, bool condition)
		: m_user(user)
		, m_access(access)
		, m_condition(condition)
	{
		if (m_condition)
		{
			DDraw::ScopedThreadLock lock;
			if (g_isSyncing)
			{
				m_condition = false;
				return;
			}

			beginAccess(user, access);
		}
	}

	AccessGuard::~AccessGuard()
	{
		if (m_condition)
		{
			DDraw::ScopedThreadLock lock;
			endAccess(m_user, m_access);
		}
	}

	DDrawAccessGuard::DDrawAccessGuard(Access access, bool condition)
		: AccessGuard(USER_DDRAW, access, condition)
	{
	}

	GdiAccessGuard::GdiAccessGuard(Access access, bool condition)
		: AccessGuard(USER_GDI, access, condition)
	{
	}
}
