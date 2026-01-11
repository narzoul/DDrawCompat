#include <map>

#include <Config/Settings/CompatFixes.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/Surfaces/TagSurface.h>

namespace
{
	std::map<DDRAWI_DIRECTDRAW_LCL*, DDraw::TagSurface*> g_ddObjects;
}

namespace DDraw
{
	TagSurface::TagSurface(DWORD origFlags, DWORD origCaps, DDRAWI_DIRECTDRAW_LCL* ddLcl)
		: Surface(origFlags, origCaps)
		, m_ddInt{}
		, m_exclusiveOwnerThreadId(0)
		, m_fullscreenWindow(nullptr)
		, m_fullscreenWindowStyle(0)
		, m_fullscreenWindowExStyle(0)
	{
		LOG_FUNC("TagSurface::TagSurface", Compat::hex(origFlags), Compat::hex(origCaps), ddLcl);
		m_ddInt.lpVtbl = &CompatVtable<IDirectDraw>::s_origVtable;
		m_ddInt.lpLcl = ddLcl;
		m_ddInt.dwIntRefCnt = 1;
	}

	TagSurface::~TagSurface()
	{
		LOG_FUNC("TagSurface::~TagSurface", m_ddInt.lpLcl);
		setFullscreenWindow(nullptr);
		g_ddObjects.erase(m_ddInt.lpLcl);
	}

	HRESULT TagSurface::create(CompatRef<IDirectDraw> dd)
	{
		DDSURFACEDESC desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
		desc.dwWidth = 1;
		desc.dwHeight = 1;
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;

		auto ddLcl = DDraw::DirectDraw::getInt(dd.get()).lpLcl;
		auto privateData(std::make_unique<TagSurface>(desc.dwFlags, desc.ddsCaps.dwCaps, ddLcl));
		g_ddObjects[ddLcl] = privateData.get();

		IDirectDrawSurface* surface = nullptr;
		s_inCreateSurface = true;
		const HRESULT result = Surface::create(dd, desc, surface, std::move(privateData));
		s_inCreateSurface = false;
		return result;
	}

	TagSurface* TagSurface::findFullscreenWindow(HWND hwnd)
	{
		for (auto& pair : g_ddObjects)
		{
			if (pair.second->m_fullscreenWindow &&
				(!hwnd || hwnd == pair.second->m_fullscreenWindow))
			{
				return pair.second;
			}
		}
		return nullptr;
	}

	TagSurface* TagSurface::get(DDRAWI_DIRECTDRAW_LCL* ddLcl)
	{
		auto it = g_ddObjects.find(ddLcl);
		if (it != g_ddObjects.end())
		{
			return it->second;
		}
		return nullptr;
	}

	TagSurface* TagSurface::get(CompatRef<IDirectDraw> dd)
	{
		auto ddLcl = DDraw::DirectDraw::getInt(dd.get()).lpLcl;
		auto tagSurface = get(ddLcl);
		if (tagSurface)
		{
			return tagSurface;
		}

		if (FAILED(create(dd)))
		{
			return nullptr;
		}
		return g_ddObjects[ddLcl];
	}

	CompatPtr<IDirectDraw7> TagSurface::getDD()
	{
		return CompatPtr<IDirectDraw7>::from(reinterpret_cast<IDirectDraw*>(&m_ddInt));
	}

	void TagSurface::setFullscreenWindow(HWND hwnd)
	{
		if (m_fullscreenWindow == hwnd)
		{
			return;
		}
		HWND prevFullscreenWindow = m_fullscreenWindow;
		m_fullscreenWindow = hwnd;
		m_exclusiveOwnerThreadId = hwnd ? GetCurrentThreadId() : 0;

		if (Config::compatFixes.get().nowindowborders)
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

	bool TagSurface::s_inCreateSurface = false;
}
