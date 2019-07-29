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
				D3DDDIARG_LOCK lockData = {};
				lockData.hResource = gdiResource;
				lockData.Flags.ReadOnly = ACCESS_READ == access;
				gdiResource->lock(lockData);

				D3DDDIARG_UNLOCK unlockData = {};
				unlockData.hResource = gdiResource;
				gdiResource->unlock(unlockData);
			}
		}
	}

	AccessGuard::~AccessGuard()
	{
		if (m_condition && ACCESS_WRITE == m_access)
		{
			D3dDdi::ScopedCriticalSection lock;
			auto gdiResource = D3dDdi::Device::getGdiResource();
			if (!gdiResource || DDraw::PrimarySurface::getFrontResource() == *gdiResource)
			{
				DDraw::RealPrimarySurface::gdiUpdate();
			}
		}
	}
}
