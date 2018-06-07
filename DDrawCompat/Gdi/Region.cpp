#include <utility>

#include "Gdi/Region.h"
#include "Win32/DisplayMode.h"

namespace
{
	BOOL CALLBACK addMonitorRectToRegion(
		HMONITOR /*hMonitor*/, HDC /*hdcMonitor*/, LPRECT lprcMonitor, LPARAM dwData)
	{
		Gdi::Region& virtualScreenRegion = *reinterpret_cast<Gdi::Region*>(dwData);
		Gdi::Region monitorRegion(*lprcMonitor);
		virtualScreenRegion |= monitorRegion;
		return TRUE;		
	}

	Gdi::Region calculateVirtualScreenRegion()
	{
		Gdi::Region region;
		EnumDisplayMonitors(nullptr, nullptr, addMonitorRectToRegion, reinterpret_cast<LPARAM>(&region));
		return region;
	}

	Gdi::Region combineRegions(const Gdi::Region& rgn1, const Gdi::Region& rgn2, int mode)
	{
		Gdi::Region region;
		CombineRgn(region, rgn1, rgn2, mode);
		return region;
	}
}

namespace Gdi
{
	Region::Region(const RECT& rect)
		: m_region(CreateRectRgnIndirect(&rect))
	{
	}

	Region::~Region()
	{
		if (m_region)
		{
			DeleteObject(m_region);
		}
	}

	Region::Region(const Region& other)
		: Region()
	{
		CombineRgn(m_region, other, nullptr, RGN_COPY);
	}

	Region::Region(Region&& other)
		: m_region(other.m_region)
	{
		other.m_region = nullptr;
	}

	Region& Region::operator=(Region other)
	{
		swap(*this, other);
		return *this;
	}

	bool Region::isEmpty() const
	{
		return sizeof(RGNDATAHEADER) == GetRegionData(m_region, 0, nullptr);
	}

	void Region::offset(int x, int y)
	{
		OffsetRgn(m_region, x, y);
	}

	Region::operator HRGN() const
	{
		return m_region;
	}

	Region Region::operator&(const Region& other) const
	{
		return combineRegions(*this, other, RGN_AND);
	}

	Region Region::operator|(const Region& other) const
	{
		return combineRegions(*this, other, RGN_OR);
	}

	Region Region::operator-(const Region& other) const
	{
		return combineRegions(*this, other, RGN_DIFF);
	}

	Region Region::operator&=(const Region& other)
	{
		return combine(other, RGN_AND);
	}

	Region Region::operator|=(const Region& other)
	{
		return combine(other, RGN_OR);
	}

	Region Region::operator-=(const Region& other)
	{
		return combine(other, RGN_DIFF);
	}

	void swap(Region& rgn1, Region& rgn2)
	{
		std::swap(rgn1.m_region, rgn2.m_region);
	}

	Region operator&(const Region& rgn1, const Region& rgn2)
	{
		return combineRegions(rgn1, rgn2, RGN_AND);
	}

	Region operator|(const Region& rgn1, const Region& rgn2)
	{
		return combineRegions(rgn1, rgn2, RGN_OR);
	}

	Region operator-(const Region& rgn1, const Region& rgn2)
	{
		return combineRegions(rgn1, rgn2, RGN_DIFF);
	}

	Region& Region::combine(const Region& other, int mode)
	{
		CombineRgn(m_region, m_region, other, mode);
		return *this;
	}

	const Region& getVirtualScreenRegion()
	{
		static Region virtualScreenRegion;
		static ULONG displaySettingsUniqueness = Win32::DisplayMode::queryDisplaySettingsUniqueness() - 1;
		const ULONG currentDisplaySettingsUniqueness = Win32::DisplayMode::queryDisplaySettingsUniqueness();
		if (currentDisplaySettingsUniqueness != displaySettingsUniqueness)
		{
			virtualScreenRegion = calculateVirtualScreenRegion();
			displaySettingsUniqueness = currentDisplaySettingsUniqueness;
		}
		return virtualScreenRegion;
	}
}
