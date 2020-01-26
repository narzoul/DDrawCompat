#pragma once

#include <Windows.h>

namespace Gdi
{
	class Region
	{
	public:
		Region(HRGN rgn);
		Region(const RECT& rect = RECT{ 0, 0, 0, 0 });
		~Region();
		Region(const Region& other);
		Region(Region&& other);
		Region& operator=(Region other);

		bool isEmpty() const;
		void offset(int x, int y);

		operator HRGN() const;

		Region operator&(const Region& other) const;
		Region operator|(const Region& other) const;
		Region operator-(const Region& other) const;

		Region operator&=(const Region& other);
		Region operator|=(const Region& other);
		Region operator-=(const Region& other);

		friend void swap(Region& rgn1, Region& rgn2);

	private:
		Region& combine(const Region& other, int mode);

		HRGN m_region;
	};
}
