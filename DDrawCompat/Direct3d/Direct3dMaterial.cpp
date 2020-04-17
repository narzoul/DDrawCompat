#include <Direct3d/Direct3dMaterial.h>

namespace Direct3d
{
	template <typename TDirect3dMaterial>
	void Direct3dMaterial<TDirect3dMaterial>::setCompatVtable(Vtable<TDirect3dMaterial>& /*vtable*/)
	{
	}

	template Direct3dMaterial<IDirect3DMaterial>;
	template Direct3dMaterial<IDirect3DMaterial2>;
	template Direct3dMaterial<IDirect3DMaterial3>;
}
