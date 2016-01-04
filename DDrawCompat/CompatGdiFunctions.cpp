#include <algorithm>
#include <unordered_map>
#include <vector>

#include "CompatDirectDrawSurface.h"
#include "CompatGdi.h"
#include "CompatGdiDc.h"
#include "CompatGdiFunctions.h"
#include "CompatPrimarySurface.h"
#include "DDrawLog.h"
#include "DDrawScopedThreadLock.h"
#include "RealPrimarySurface.h"

#include <detours.h>

namespace
{
	using CompatGdiDc::CachedDc;

	struct CompatDc : CachedDc
	{
		CompatDc(const CachedDc& cachedDc) : CachedDc(cachedDc) {}
		HGDIOBJ origFont;
		HGDIOBJ origBrush;
		HGDIOBJ origPen;
	};

	std::unordered_map<void*, const char*> g_funcNames;
	std::vector<CompatDc> g_usedCompatDcs;
	DWORD* g_usedCompatDcCount = nullptr;

	template <typename Result, typename... Params>
	using FuncPtr = Result(WINAPI *)(Params...);

	template <typename OrigFuncPtr, OrigFuncPtr origFunc>
	OrigFuncPtr& getOrigFuncPtr()
	{
		static OrigFuncPtr origFuncPtr = origFunc;
		return origFuncPtr;
	}

	template <typename T>
	T replaceDc(T t)
	{
		return t;
	}

	HDC replaceDc(HDC dc)
	{
		auto it = std::find_if(g_usedCompatDcs.begin(), g_usedCompatDcs.end(),
			[dc](const CompatDc& compatDc) { return compatDc.dc == dc; });
		if (it != g_usedCompatDcs.end())
		{
			return it->dc;
		}

		CompatDc compatDc = CompatGdiDc::getDc(dc);
		if (!compatDc.dc)
		{
			return dc;
		}

		compatDc.origFont = SelectObject(compatDc.dc, GetCurrentObject(dc, OBJ_FONT));
		compatDc.origBrush = SelectObject(compatDc.dc, GetCurrentObject(dc, OBJ_BRUSH));
		compatDc.origPen = SelectObject(compatDc.dc, GetCurrentObject(dc, OBJ_PEN));
		SetTextColor(compatDc.dc, GetTextColor(dc));
		SetBkColor(compatDc.dc, GetBkColor(dc));
		SetBkMode(compatDc.dc, GetBkMode(dc));

		g_usedCompatDcs.push_back(compatDc);
		++*g_usedCompatDcCount;
		return compatDc.dc;
	}

	template <typename OrigFuncPtr, OrigFuncPtr origFunc, typename Result, typename... Params>
	Result WINAPI compatGdiFunc(Params... params)
	{
		CompatGdi::GdiScopedThreadLock gdiLock;
		Compat::DDrawScopedThreadLock ddLock;

		DWORD usedCompatDcCount = 0;
		g_usedCompatDcCount = &usedCompatDcCount;

		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		if (FAILED(CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.Lock(
			CompatPrimarySurface::surface, nullptr, &desc, DDLOCK_WAIT, nullptr)))
		{
			return getOrigFuncPtr<OrigFuncPtr, origFunc>()(params...);
		}

		Result result = getOrigFuncPtr<OrigFuncPtr, origFunc>()(replaceDc(params)...);
		GdiFlush();

		CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.Unlock(
			CompatPrimarySurface::surface, nullptr);

		if (0 != usedCompatDcCount)
		{
			RealPrimarySurface::update();
		}

		for (DWORD i = 0; i < usedCompatDcCount; ++i)
		{
			CompatDc& compatDc = g_usedCompatDcs.back();
			SelectObject(compatDc.dc, compatDc.origFont);
			SelectObject(compatDc.dc, compatDc.origBrush);
			SelectObject(compatDc.dc, compatDc.origPen);

			CompatGdiDc::releaseDc(compatDc);
			g_usedCompatDcs.pop_back();
		}

		return result;
	}

	template <typename OrigFuncPtr, OrigFuncPtr origFunc, typename Result, typename... Params>
	OrigFuncPtr getCompatGdiFuncPtr(FuncPtr<Result, Params...>&)
	{
		return &compatGdiFunc<OrigFuncPtr, origFunc, Result, Params...>;
	}

	FARPROC getProcAddress(HMODULE module, const char* procName)
	{
		if (!module || !procName)
		{
			return nullptr;
		}

		PIMAGE_DOS_HEADER dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
		if (IMAGE_DOS_SIGNATURE != dosHeader->e_magic) {
			return nullptr;
		}
		char* moduleBase = reinterpret_cast<char*>(module);

		PIMAGE_NT_HEADERS ntHeader = reinterpret_cast<PIMAGE_NT_HEADERS>(
			reinterpret_cast<char*>(dosHeader) + dosHeader->e_lfanew);
		if (IMAGE_NT_SIGNATURE != ntHeader->Signature)
		{
			return nullptr;
		}

		PIMAGE_EXPORT_DIRECTORY exportDir = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(
			moduleBase + ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);

		DWORD* rvaOfNames = reinterpret_cast<DWORD*>(moduleBase + exportDir->AddressOfNames);

		for (DWORD i = 0; i < exportDir->NumberOfNames; ++i)
		{
			if (0 == strcmp(procName, moduleBase + rvaOfNames[i]))
			{
				WORD* nameOrds = reinterpret_cast<WORD*>(moduleBase + exportDir->AddressOfNameOrdinals);
				DWORD* rvaOfFunctions = reinterpret_cast<DWORD*>(moduleBase + exportDir->AddressOfFunctions);
				return reinterpret_cast<FARPROC>(moduleBase + rvaOfFunctions[nameOrds[i]]);
			}
		}

		return nullptr;
	}

	template <typename OrigFuncPtr, OrigFuncPtr origFunc>
	void hookGdiFunction(const char* moduleName, const char* funcName)
	{
		OrigFuncPtr& origFuncPtr = getOrigFuncPtr<OrigFuncPtr, origFunc>();
		origFuncPtr = reinterpret_cast<OrigFuncPtr>(getProcAddress(GetModuleHandle(moduleName), funcName));
		OrigFuncPtr newFuncPtr = getCompatGdiFuncPtr<OrigFuncPtr, origFunc>(origFuncPtr);
		DetourAttach(reinterpret_cast<void**>(&origFuncPtr), newFuncPtr);
		g_funcNames[origFunc] = funcName;
	}
}

void CompatGdiFunctions::hookGdiFunctions()
{
#define HOOK_GDI_FUNCTION(module, func) hookGdiFunction<decltype(&func), &func>(#module, #func);

#define HOOK_GDI_TEXT_FUNCTION(module, func) \
	HOOK_GDI_FUNCTION(module, func##A); \
	HOOK_GDI_FUNCTION(module, func##W)

	DetourTransactionBegin();
	HOOK_GDI_FUNCTION(gdi32, BitBlt);
	DetourTransactionCommit();

#undef HOOK_GDI_TEXT_FUNCTION
#undef HOOK_GDI_FUNCTION
}
