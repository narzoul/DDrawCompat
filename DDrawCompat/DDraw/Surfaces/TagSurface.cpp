#include <map>

#include <Config/Config.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/Surfaces/TagSurface.h>

namespace
{
	std::map<void*, DDraw::TagSurface*> g_ddObjects;
}

namespace DDraw
{
	TagSurface::TagSurface(DWORD origCaps, void* ddObject)
		: Surface(origCaps)
		, m_ddObject(ddObject)
		, m_fullscreenWindow(nullptr)
		, m_fullscreenWindowStyle(0)
		, m_fullscreenWindowExStyle(0)
	{
	}

	TagSurface::~TagSurface()
	{
		setFullscreenWindow(nullptr);
		g_ddObjects.erase(m_ddObject);
	}

	HRESULT TagSurface::create(CompatRef<IDirectDraw> dd)
	{
		DDSURFACEDESC desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
		desc.dwWidth = 1;
		desc.dwHeight = 1;
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;

		auto ddObject = DDraw::DirectDraw::getDdObject(dd.get());
		auto privateData(std::make_unique<TagSurface>(desc.ddsCaps.dwCaps, ddObject));
		g_ddObjects[ddObject] = privateData.get();

		IDirectDrawSurface* surface = nullptr;
		return Surface::create(dd, desc, surface, std::move(privateData));
	}

	TagSurface* TagSurface::findFullscreenWindow(HWND hwnd)
	{
		for (auto& pair : g_ddObjects)
		{
			if (hwnd == pair.second->m_fullscreenWindow)
			{
				return pair.second;
			}
		}
		return nullptr;
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

	TagSurface* TagSurface::get(CompatRef<IDirectDraw> dd)
	{
		auto ddObject = DDraw::DirectDraw::getDdObject(dd.get());
		auto it = g_ddObjects.find(ddObject);
		if (it != g_ddObjects.end())
		{
			return it->second;
		}

		if (FAILED(create(dd)))
		{
			return nullptr;
		}
		return g_ddObjects[ddObject];
	}

	void TagSurface::setFullscreenWindow(HWND hwnd)
	{
		if (m_fullscreenWindow == hwnd)
		{
			return;
		}
		HWND prevFullscreenWindow = m_fullscreenWindow;
		m_fullscreenWindow = hwnd;

		if (Config::removeBorders.get())
		{
			if (hwnd)
			{
				setWindowStyle(CALL_ORIG_FUNC(GetWindowLongA)(hwnd, GWL_STYLE));
				setWindowExStyle(CALL_ORIG_FUNC(GetWindowLongA)(hwnd, GWL_EXSTYLE));
			}
			else if (prevFullscreenWindow)
			{
				CALL_ORIG_FUNC(SetWindowLongA)(prevFullscreenWindow, GWL_STYLE, m_fullscreenWindowStyle);
				CALL_ORIG_FUNC(SetWindowLongA)(prevFullscreenWindow, GWL_EXSTYLE, m_fullscreenWindowExStyle);
				m_fullscreenWindowStyle = 0;
				m_fullscreenWindowExStyle = 0;
			}
		}
	}

	LONG TagSurface::setWindowStyle(LONG style)
	{
		auto lastError = GetLastError();
		SetLastError(0);
		LONG prevStyle = CALL_ORIG_FUNC(SetWindowLongA)(m_fullscreenWindow, GWL_STYLE, style);
		if (0 != prevStyle || 0 == GetLastError())
		{
			CALL_ORIG_FUNC(SetWindowLongA)(m_fullscreenWindow, GWL_STYLE,
				(style | WS_POPUP) & ~(WS_BORDER | WS_DLGFRAME | WS_SYSMENU | WS_THICKFRAME));
			m_fullscreenWindowStyle = style;
			SetLastError(lastError);
		}
		return prevStyle;
	}

	LONG TagSurface::setWindowExStyle(LONG exStyle)
	{
		auto lastError = GetLastError();
		SetLastError(0);
		LONG prevExStyle = CALL_ORIG_FUNC(SetWindowLongA)(m_fullscreenWindow, GWL_EXSTYLE, exStyle);
		if (0 != prevExStyle || 0 == GetLastError())
		{
			CALL_ORIG_FUNC(SetWindowLongA)(m_fullscreenWindow, GWL_EXSTYLE,
				exStyle & ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE));
			m_fullscreenWindowExStyle = exStyle;
			SetLastError(lastError);
		}
		return prevExStyle;
	}
}
