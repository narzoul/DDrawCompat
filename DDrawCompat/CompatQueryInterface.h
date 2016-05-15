#pragma once

struct IUnknown;

namespace Compat
{
	template <typename Intf>
	void queryInterface(Intf& origIntf, Intf*& newIntf)
	{
		newIntf = &origIntf;
		newIntf->lpVtbl->AddRef(newIntf);
	}

	void queryInterface(IUnknown&, IUnknown*&) = delete;

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
