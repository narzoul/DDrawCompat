#pragma once

#include "Common/CompatVtable.h"
#include "DirectDrawPaletteVtblVisitor.h"

namespace DDraw
{
	class DirectDrawPalette : public CompatVtable<DirectDrawPalette, IDirectDrawPalette>
	{
	public:
		static void setCompatVtable(IDirectDrawPaletteVtbl& vtable);

		static HRESULT STDMETHODCALLTYPE SetEntries(
			IDirectDrawPalette* This,
			DWORD dwFlags,
			DWORD dwStartingEntry,
			DWORD dwCount,
			LPPALETTEENTRY lpEntries);

		static void waitForNextUpdate();
	};
}
