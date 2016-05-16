#pragma once

#include <type_traits>

#include "CompatVtable.h"
#include "DDrawTypes.h"
#include "DirectDrawVtblVisitor.h"

template <typename TDirectDraw>
class CompatDirectDraw : public CompatVtable<CompatDirectDraw<TDirectDraw>, TDirectDraw>
{
public:
	typedef typename Types<TDirectDraw>::TCreatedSurface TSurface;
	typedef typename Types<TDirectDraw>::TSurfaceDesc TSurfaceDesc;

	static void setCompatVtable(Vtable<TDirectDraw>& vtable);

	static HRESULT STDMETHODCALLTYPE CreateSurface(
		TDirectDraw* This,
		TSurfaceDesc* lpDDSurfaceDesc,
		TSurface** lplpDDSurface,
		IUnknown* pUnkOuter);

	static HRESULT STDMETHODCALLTYPE RestoreDisplayMode(TDirectDraw* This);
	static HRESULT STDMETHODCALLTYPE SetCooperativeLevel(TDirectDraw* This, HWND hWnd, DWORD dwFlags);

	template <typename... Params>
	static HRESULT STDMETHODCALLTYPE SetDisplayMode(
		TDirectDraw* This,
		DWORD dwWidth,
		DWORD dwHeight,
		DWORD dwBPP,
		Params... params);

	static const IID& s_iid;
};

namespace Compat
{
	template <typename Intf>
	struct IsDirectDrawIntf : std::false_type {};

	template<> struct IsDirectDrawIntf<IDirectDraw> : std::true_type {};
	template<> struct IsDirectDrawIntf<IDirectDraw2> : std::true_type {};
	template<> struct IsDirectDrawIntf<IDirectDraw4> : std::true_type {};
	template<> struct IsDirectDrawIntf<IDirectDraw7> : std::true_type {};

	template <typename NewIntf, typename OrigIntf>
	std::enable_if_t<IsDirectDrawIntf<NewIntf>::value && IsDirectDrawIntf<OrigIntf>::value>
		queryInterface(OrigIntf& origIntf, NewIntf*& newIntf)
	{
		CompatDirectDraw<OrigIntf>::s_origVtable.QueryInterface(
			&origIntf, CompatDirectDraw<NewIntf>::s_iid, reinterpret_cast<void**>(&newIntf));
	}

	template <typename NewIntf>
	std::enable_if_t<IsDirectDrawIntf<NewIntf>::value>
		queryInterface(IUnknown& origIntf, NewIntf*& newIntf)
	{
		CompatDirectDraw<IDirectDraw>::s_origVtable.QueryInterface(
			reinterpret_cast<IDirectDraw*>(&origIntf),
			CompatDirectDraw<NewIntf>::s_iid, reinterpret_cast<void**>(&newIntf));
	}

	template <typename OrigIntf>
	std::enable_if_t<IsDirectDrawIntf<OrigIntf>::value>
		queryInterface(OrigIntf& origIntf, IUnknown*& newIntf)
	{
		CompatDirectDraw<OrigIntf>::s_origVtable.QueryInterface(
			&origIntf, IID_IUnknown, reinterpret_cast<void**>(&newIntf));
	}
}
