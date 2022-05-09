#pragma once

#include <vector>

#include <Windows.h>

#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>

namespace DDraw
{
	namespace DirectDrawSurface
	{
		std::vector<CompatPtr<IDirectDrawSurface7>> getAllAttachedSurfaces(CompatRef<IDirectDrawSurface7> surface);

		template <typename TSurface>
		void* getSurfaceObject(TSurface& surface)
		{
			return reinterpret_cast<void**>(&surface)[1];
		}

		template <typename TSurface>
		DWORD& getFlags(TSurface& surface)
		{
			return reinterpret_cast<DWORD**>(&surface)[1][7];
		}

		template <typename TSurface>
		HANDLE getRuntimeResourceHandle(TSurface& surface)
		{
			return reinterpret_cast<HANDLE**>(&surface)[1][2];
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
