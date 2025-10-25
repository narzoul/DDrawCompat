#include <Common/CompatVtable.h>
#include <DDraw/ScopedThreadLock.h>
#include <Direct3d/Direct3dVertexBuffer.h>
#include <Direct3d/Visitors/Direct3dVertexBufferVtblVisitor.h>

namespace
{
	template <typename TDirect3DVertexBuffer, typename TDirect3DDevice>
	HRESULT STDMETHODCALLTYPE optimize(TDirect3DVertexBuffer* /*This*/, TDirect3DDevice* /*lpD3DDevice*/, DWORD /*dwFlags*/)
	{
		return D3D_OK;
	}

	template <typename Vtable>
	constexpr void setCompatVtable(Vtable& vtable)
	{
		vtable.Optimize = &optimize;
	}
}

namespace Direct3d
{
	namespace Direct3dVertexBuffer
	{
		template <typename Vtable>
		void hookVtable(const Vtable& vtable)
		{
			CompatVtable<Vtable>::template hookVtable<DDraw::ScopedThreadLock>(vtable);
		}

		template void hookVtable(const IDirect3DVertexBufferVtbl&);
		template void hookVtable(const IDirect3DVertexBuffer7Vtbl&);
	}
}
