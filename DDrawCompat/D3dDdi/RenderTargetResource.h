#pragma once

#define CINTERFACE

#include <d3d.h>
#include <d3dumddi.h>

#include <set>
#include <vector>

namespace D3dDdi
{
	class RenderTargetResource
	{
	public:
		RenderTargetResource(HANDLE device, HANDLE resource, D3DDDIFORMAT format, UINT surfaceCount);

		HRESULT lock(D3DDDIARG_LOCK& data);
		HRESULT unlock(const D3DDDIARG_UNLOCK& data);

		HANDLE getHandle() const { return m_resource; }
		bool hasLockedSubResources() { return !m_lockedSubResources.empty(); }
		void prepareForRendering(UINT subResourceIndex = UINT_MAX);

	private:
		struct SubResource
		{
			RenderTargetResource& parent;
			void* surfacePtr;
			UINT pitch;
			bool isLocked;

			SubResource(RenderTargetResource& parent);
		};

		void prepareSubResourceForRendering(UINT subResourceIndex);

		HANDLE m_device;
		HANDLE m_resource;
		UINT m_bytesPerPixel;
		std::vector<SubResource> m_subResources;
		std::set<UINT> m_lockedSubResources;
	};
}
