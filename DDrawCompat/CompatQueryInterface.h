#pragma once

#include <type_traits>

#include "CompatVtable.h"

namespace Compat
{
	template <typename Intf>
	struct GetBaseIntf
	{
		typedef Intf Type;
	};

#define DEFINE_BASE_INTF(Intf, BaseIntf) \
	template<> struct GetBaseIntf<Intf> { typedef BaseIntf Type; }

	DEFINE_BASE_INTF(IDirectDraw2, IDirectDraw);
	DEFINE_BASE_INTF(IDirectDraw4, IDirectDraw);
	DEFINE_BASE_INTF(IDirectDraw7, IDirectDraw);
	DEFINE_BASE_INTF(IDirectDrawSurface2, IDirectDrawSurface);
	DEFINE_BASE_INTF(IDirectDrawSurface3, IDirectDrawSurface);
	DEFINE_BASE_INTF(IDirectDrawSurface4, IDirectDrawSurface);
	DEFINE_BASE_INTF(IDirectDrawSurface7, IDirectDrawSurface);

#undef DEFINE_BASE_INTF

	template <typename Intf>
	const IID& getIntfId();
	
#define DEFINE_INTF_ID(Intf) \
	template<> inline const IID& getIntfId<Intf>() { return IID_##Intf; }

	DEFINE_INTF_ID(IDirectDraw);
	DEFINE_INTF_ID(IDirectDraw2);
	DEFINE_INTF_ID(IDirectDraw4);
	DEFINE_INTF_ID(IDirectDraw7);
	DEFINE_INTF_ID(IDirectDrawSurface);
	DEFINE_INTF_ID(IDirectDrawSurface2);
	DEFINE_INTF_ID(IDirectDrawSurface3);
	DEFINE_INTF_ID(IDirectDrawSurface4);
	DEFINE_INTF_ID(IDirectDrawSurface7);
	DEFINE_INTF_ID(IDirectDrawPalette);
	DEFINE_INTF_ID(IDirectDrawClipper);
	DEFINE_INTF_ID(IDirectDrawColorControl);
	DEFINE_INTF_ID(IDirectDrawGammaControl);

#undef DEFINE_INTF_ID

	template <typename Intf>
	void queryInterface(Intf& origIntf, Intf*& newIntf)
	{
		newIntf = &origIntf;
		newIntf->lpVtbl->AddRef(newIntf);
	}

	template <typename NewIntf>
	void queryInterface(IUnknown& origIntf, NewIntf*& newIntf)
	{
		CompatVtableBase<NewIntf>::getOrigVtable(reinterpret_cast<NewIntf&>(origIntf)).QueryInterface(
			reinterpret_cast<NewIntf*>(&origIntf),
			getIntfId<NewIntf>(),
			reinterpret_cast<void**>(&newIntf));
	}

	template <typename OrigIntf>
	void queryInterface(OrigIntf& origIntf, IUnknown*& newIntf)
	{
		CompatVtableBase<OrigIntf>::getOrigVtable(origIntf).QueryInterface(
			&origIntf, IID_IUnknown, reinterpret_cast<void**>(&newIntf));
	}

	template <typename NewIntf, typename OrigIntf>
	std::enable_if_t<std::is_same<
		typename GetBaseIntf<NewIntf>::Type,
		typename GetBaseIntf<OrigIntf>::Type>::value>
	queryInterface(OrigIntf& origIntf, NewIntf*& newIntf)
	{
		CompatVtableBase<OrigIntf>::getOrigVtable(origIntf).QueryInterface(
			&origIntf, getIntfId<NewIntf>(), reinterpret_cast<void**>(&newIntf));
	}

	template <typename NewIntf, typename OrigIntf>
	NewIntf* queryInterface(OrigIntf* origIntf)
	{
		if (!origIntf)
		{
			return nullptr;
		}

		NewIntf* newIntf = nullptr;
		queryInterface(*origIntf, newIntf);
		return newIntf;
	}
}
