#include <D3dDdi/Device.h>
#include <D3dDdi/Resource.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <D3dDdi/SurfaceRepository.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <Gdi/CompatDc.h>
#include <Gdi/Dc.h>

namespace Gdi
{
	CompatDc::CompatDc(HDC dc, bool isReadOnly)
		: m_origDc(dc)
		, m_compatDc(Gdi::Dc::getDc(dc))
		, m_isReadOnly(isReadOnly)
	{
		if (m_compatDc)
		{
			D3dDdi::ScopedCriticalSection lock;
			auto gdiResource = D3dDdi::Device::getGdiResource();
			if (gdiResource)
			{
				gdiResource->getDevice().flushPrimitives();
				D3dDdi::SurfaceRepository::enableSurfaceCheck(false);
				if (isReadOnly)
				{
					gdiResource->prepareForCpuRead(0);
				}
				else
				{
					gdiResource->prepareForCpuWrite(0);
				}
				D3dDdi::SurfaceRepository::enableSurfaceCheck(true);
			}
		}
		else
		{
			m_compatDc = m_origDc;
		}
	}

	CompatDc::~CompatDc()
	{
		if (m_compatDc != m_origDc)
		{
			D3dDdi::ScopedCriticalSection lock;
			auto gdiResource = D3dDdi::Device::getGdiResource();
			if (!m_isReadOnly && (!gdiResource || DDraw::PrimarySurface::getFrontResource() == *gdiResource))
			{
				DDraw::RealPrimarySurface::scheduleUpdate();
			}
			Gdi::Dc::releaseDc(m_origDc);
		}
	}
}
