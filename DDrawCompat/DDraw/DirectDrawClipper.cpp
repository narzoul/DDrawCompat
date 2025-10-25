#include <vector>

#include <Common/CompatVtable.h>
#include <DDraw/DirectDrawClipper.h>
#include <DDraw/ScopedThreadLock.h>
#include <DDraw/Visitors/DirectDrawClipperVtblVisitor.h>

namespace
{
	template<>
	constexpr void setCompatVtable(IDirectDrawClipperVtbl& /*vtable*/)
	{
	}
}

namespace DDraw
{
	namespace DirectDrawClipper
	{
		HRGN getClipRgn(CompatRef<IDirectDrawClipper> clipper)
		{
			std::vector<unsigned char> rgnData;
			DWORD size = 0;
			clipper->GetClipList(&clipper, nullptr, nullptr, &size);
			rgnData.resize(size);
			clipper->GetClipList(&clipper, nullptr, reinterpret_cast<RGNDATA*>(rgnData.data()), &size);
			return ExtCreateRegion(nullptr, size, reinterpret_cast<RGNDATA*>(rgnData.data()));
		}

		HRESULT setClipRgn(CompatRef<IDirectDrawClipper> clipper, HRGN rgn)
		{
			std::vector<unsigned char> rgnData;
			rgnData.resize(GetRegionData(rgn, 0, nullptr));
			GetRegionData(rgn, rgnData.size(), reinterpret_cast<RGNDATA*>(rgnData.data()));
			return clipper->SetClipList(&clipper, reinterpret_cast<RGNDATA*>(rgnData.data()), 0);
		}

		void hookVtable(const IDirectDrawClipperVtbl& vtable)
		{
			CompatVtable<IDirectDrawClipperVtbl>::hookVtable<ScopedThreadLock>(vtable);
		}
	}
}
