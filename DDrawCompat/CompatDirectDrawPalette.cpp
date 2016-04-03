#include <cstring>

#include "CompatDirectDrawPalette.h"
#include "CompatPrimarySurface.h"
#include "RealPrimarySurface.h"

void CompatDirectDrawPalette::setCompatVtable(IDirectDrawPaletteVtbl& vtable)
{
	vtable.SetEntries = &SetEntries;
}

HRESULT STDMETHODCALLTYPE CompatDirectDrawPalette::SetEntries(
	IDirectDrawPalette* This,
	DWORD dwFlags,
	DWORD dwStartingEntry,
	DWORD dwCount,
	LPPALETTEENTRY lpEntries)
{
	if (This == CompatPrimarySurface::palette && lpEntries && dwStartingEntry + dwCount <= 256 &&
		0 == std::memcmp(&CompatPrimarySurface::paletteEntries[dwStartingEntry],
			lpEntries, dwCount * sizeof(PALETTEENTRY)))
	{
		return DD_OK;
	}

	HRESULT result = s_origVtable.SetEntries(This, dwFlags, dwStartingEntry, dwCount, lpEntries);
	if (This == CompatPrimarySurface::palette && SUCCEEDED(result))
	{
		std::memcpy(&CompatPrimarySurface::paletteEntries[dwStartingEntry], lpEntries,
			dwCount * sizeof(PALETTEENTRY));
		RealPrimarySurface::updatePalette(dwStartingEntry, dwCount);
	}
	return result;
}
