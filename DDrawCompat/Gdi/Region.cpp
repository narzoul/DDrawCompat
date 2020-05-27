#include <utility>

#include <Common/Hook.h>
#include <Gdi/Region.h>

namespace
{
	Gdi::Region combineRegions(const Gdi::Region& rgn1, const Gdi::Region& rgn2, int mode)
	{
		Gdi::Region region;
		CombineRgn(region, rgn1, rgn2, mode);
		return region;
	}
}

namespace Gdi
{
	Region::Region(HRGN rgn)
		: m_region(rgn)
	{
	}

	Region::Region(const RECT& rect)
		: m_region(CreateRectRgnIndirect(&rect))
	{
	}

	Region::Region(HWND hwnd)
		: m_region(CreateRectRgn(0, 0, 0, 0))
	{
		HDC dc = GetWindowDC(hwnd);
		GetRandomRgn(dc, m_region, SYSRGN);
		CALL_ORIG_FUNC(ReleaseDC)(hwnd, dc);
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

	HRGN Region::release()
	{
		HRGN rgn = m_region;
		m_region = nullptr;
		return rgn;
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
}
