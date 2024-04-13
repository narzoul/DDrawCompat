#pragma once

#include <type_traits>
#include <vector>

#include <Windows.h>
#include <ddrawi.h>

#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>

namespace D3dDdi
{
	class SurfaceRepository;
}

namespace DDraw
{
	namespace DirectDrawSurface
	{
		template <typename TSurface>
		struct IsSurface : std::false_type {};

		template <> struct IsSurface<IDirectDrawSurface> : std::true_type {};
		template <> struct IsSurface<IDirectDrawSurface2> : std::true_type {};
		template <> struct IsSurface<IDirectDrawSurface3> : std::true_type {};
		template <> struct IsSurface<IDirectDrawSurface4> : std::true_type {};
		template <> struct IsSurface<IDirectDrawSurface7> : std::true_type {};

		std::vector<CompatPtr<IDirectDrawSurface7>> getAllAttachedSurfaces(CompatRef<IDirectDrawSurface7> surface);

		template <typename TSurface, typename = std::enable_if_t<IsSurface<TSurface>::value>>
		DDRAWI_DDRAWSURFACE_INT& getInt(TSurface& surface)
		{
			return reinterpret_cast<DDRAWI_DDRAWSURFACE_INT&>(surface);
		}

		template <typename TSurface, typename = std::enable_if_t<IsSurface<TSurface>::value>>
		HANDLE getRuntimeResourceHandle(TSurface& surface)
		{
			return reinterpret_cast<HANDLE>(getInt(surface).lpLcl->hDDSurface);
		}

		template <typename TSurface, typename = std::enable_if_t<IsSurface<TSurface>::value>>
		HANDLE& getDriverResourceHandle(TSurface& surface)
		{
			return reinterpret_cast<HANDLE*>(getRuntimeResourceHandle(surface))[0];
		}

		template <typename TSurface, typename = std::enable_if_t<IsSurface<TSurface>::value>>
		UINT& getSubResourceIndex(TSurface& surface)
		{
			return reinterpret_cast<UINT*>(getRuntimeResourceHandle(surface))[1];
		}

		D3dDdi::SurfaceRepository* getSurfaceRepository(HANDLE resource);

		template <typename TSurface, typename = std::enable_if_t<IsSurface<TSurface>::value>>
		D3dDdi::SurfaceRepository* getSurfaceRepository(TSurface& surface)
		{
			return getSurfaceRepository(getDriverResourceHandle(surface));
		}

		template <typename Vtable>
		void hookVtable(const Vtable& vtable);
	}
}
