#include <map>

#include <DDraw/DirectDraw.h>
#include <DDraw/Surfaces/TagSurface.h>

namespace
{
	std::map<void*, DDraw::TagSurface*> g_ddObjects;
}

namespace DDraw
{
	HRESULT TagSurface::create(CompatRef<IDirectDraw> dd)
	{
		auto ddObject = DDraw::DirectDraw::getDdObject(dd.get());
		if (g_ddObjects.find(ddObject) != g_ddObjects.end())
		{
			return DD_OK;
		}

		DDSURFACEDESC desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
		desc.dwWidth = 1;
		desc.dwHeight = 1;
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;

		auto privateData(std::make_unique<TagSurface>(desc.ddsCaps.dwCaps, ddObject));
		g_ddObjects[ddObject] = privateData.get();

		IDirectDrawSurface* surface = nullptr;
		return Surface::create(dd, desc, surface, std::move(privateData));
	}

	void TagSurface::forEachDirectDraw(std::function<void(CompatRef<IDirectDraw7>)> callback)
	{
		struct DirectDrawInterface
		{
			const void* vtable;
			void* ddObject;
			DirectDrawInterface* next;
			DWORD refCount;
		};

		for (auto ddObj : g_ddObjects)
		{
			DirectDrawInterface intf = { &CompatVtable<IDirectDraw7Vtbl>::s_origVtable, ddObj.first, nullptr, 1 };
			callback(CompatRef<IDirectDraw7>(reinterpret_cast<IDirectDraw7&>(intf)));
		}
	}

	TagSurface::~TagSurface()
	{
		g_ddObjects.erase(m_ddObject);
	}
}
