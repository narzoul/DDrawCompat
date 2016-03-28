#include "CompatDirectDraw.h"
#include "DDrawLog.h"
#include "DDrawProcs.h"
#include "DDrawRepository.h"

namespace
{
	IDirectDraw7* createDirectDraw()
	{
		IDirectDraw7* dd = nullptr;
		HRESULT result = CALL_ORIG_DDRAW(DirectDrawCreateEx, nullptr,
			reinterpret_cast<void**>(&dd), IID_IDirectDraw7, nullptr);
		if (FAILED(result))
		{
			Compat::Log() << "Failed to create a DirectDraw object in the repository: " << result;
			return nullptr;
		}

		result = dd->lpVtbl->SetCooperativeLevel(dd, nullptr, DDSCL_NORMAL);
		if (FAILED(result))
		{
			Compat::Log() << "Failed to set the cooperative level in the repository: " << result;
			dd->lpVtbl->Release(dd);
			return nullptr;
		}

		return dd;
	}
}

namespace DDrawRepository
{
	IDirectDraw7* getDirectDraw()
	{
		static IDirectDraw7* dd = createDirectDraw();
		return dd;
	}
}
