#include "D3dDdi/Device.h"
#include "D3dDdi/Resource.h"
#include "DDraw/RealPrimarySurface.h"
#include "DDraw/Surfaces/PrimarySurface.h"
#include "Gdi/AccessGuard.h"

namespace Gdi
{
	AccessGuard::AccessGuard(Access access, bool condition)
		: m_access(access)
		, m_condition(condition)
	{
		if (m_condition)
		{
			D3dDdi::ScopedCriticalSection lock;
			auto gdiResource = D3dDdi::Device::getGdiResource();
			if (gdiResource)
			{
				gdiResource->beginGdiAccess(ACCESS_READ == m_access);
			}
		}
	}

	AccessGuard::~AccessGuard()
	{
		if (m_condition)
		{
			D3dDdi::ScopedCriticalSection lock;
			auto gdiResource = D3dDdi::Device::getGdiResource();
			if (gdiResource)
			{
				gdiResource->endGdiAccess(ACCESS_READ == m_access);
			}
			if (ACCESS_WRITE == m_access &&
				(!gdiResource || DDraw::PrimarySurface::getFrontResource() == *gdiResource))
			{
				DDraw::RealPrimarySurface::scheduleUpdate();
			}
		}
	}
}
