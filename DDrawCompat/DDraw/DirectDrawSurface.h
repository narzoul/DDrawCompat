#pragma once

#include <vector>

#include <Windows.h>
#include <ddrawi.h>

#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>

namespace DDraw
{
	namespace DirectDrawSurface
	{
		std::vector<CompatPtr<IDirectDrawSurface7>> getAllAttachedSurfaces(CompatRef<IDirectDrawSurface7> surface);

		template <typename TSurface>
		DDRAWI_DDRAWSURFACE_INT& getInt(TSurface& surface)
		{
			return reinterpret_cast<DDRAWI_DDRAWSURFACE_INT&>(surface);
		}

		template <typename TSurface>
		HANDLE getRuntimeResourceHandle(TSurface& surface)
		{
			return reinterpret_cast<HANDLE>(getInt(surface).lpLcl->hDDSurface);
		}

		template <typename TSurface>
		HANDLE getDriverResourceHandle(TSurface& surface)
		{
			return reinterpret_cast<HANDLE*>(getRuntimeResourceHandle(surface))[0];
		}

		template <typename TSurface>
		UINT getSubResourceIndex(TSurface& surface)
		{
			return reinterpret_cast<UINT*>(getRuntimeResourceHandle(surface))[1];
		}

		template <typename Vtable>
		void hookVtable(const Vtable& vtable);
	}
}
