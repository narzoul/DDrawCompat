#include <sstream>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/Path.h>
#include <Win32/Log.h>

namespace
{
	template <typename T>
	class ParamConverter
	{
	public:
		template <typename Param>
		static T& convert(Param& param)
		{
			return *reinterpret_cast<T*>(&param);
		}
	};

	template <typename T>
	class ParamConverter<Compat::Out<T>>
	{
	public:
		template <typename Param>
		static Compat::Out<T> convert(Param& param)
		{
			return Compat::out(ParamConverter<T>::convert(param));
		}
	};

	template <typename CreateStruct>
	std::ostream& logCreateStruct(std::ostream& os, const CreateStruct& cs)
	{
		return Compat::LogStruct(os)
			<< Compat::hex(cs.dwExStyle)
			<< cs.lpszClass
			<< cs.lpszName
			<< Compat::hex(cs.style)
			<< cs.x
			<< cs.y
			<< cs.cx
			<< cs.cy
			<< cs.hwndParent
			<< cs.hMenu
			<< cs.hInstance
			<< cs.lpCreateParams;
	}

	template <typename DevMode>
	std::ostream& logDevMode(std::ostream& os, const DevMode& dm)
	{
		return Compat::LogStruct(os)
			<< Compat::hex(dm.dmFields)
			<< dm.dmPelsWidth
			<< dm.dmPelsHeight
			<< dm.dmBitsPerPel
			<< dm.dmDisplayFrequency
			<< Compat::hex(dm.dmDisplayFlags);
	}

	template <typename MdiCreateStruct>
	std::ostream& logMdiCreateStruct(std::ostream& os, const MdiCreateStruct& mcs)
	{
		return Compat::LogStruct(os)
			<< mcs.szClass
			<< mcs.szTitle
			<< mcs.hOwner
			<< mcs.x
			<< mcs.y
			<< mcs.cx
			<< mcs.cy
			<< Compat::hex(mcs.style)
			<< Compat::hex(mcs.lParam);
	}

	template <typename OsVersionInfoEx>
	std::ostream& logOsVersionInfoEx(std::ostream& os, const OsVersionInfoEx& vi)
	{
		return Compat::LogStruct(os)
			<< vi.dwOSVersionInfoSize
			<< vi.dwMajorVersion
			<< vi.dwMinorVersion
			<< vi.dwBuildNumber
			<< vi.dwPlatformId
			<< Compat::out(vi.szCSDVersion)
			<< vi.wServicePackMajor
			<< vi.wServicePackMinor
			<< Compat::hex(vi.wSuiteMask)
			<< static_cast<DWORD>(vi.wProductType)
			<< static_cast<DWORD>(vi.wReserved);
	}

	template <typename OsVersionInfoEx, typename OsVersionInfo>
	std::ostream& logOsVersionInfo(std::ostream& os, const OsVersionInfo& vi)
	{
		OsVersionInfoEx viEx = {};
		memcpy(&viEx, &vi, std::min<DWORD>(sizeof(viEx), vi.dwOSVersionInfoSize));
		return logOsVersionInfoEx(os, viEx);
	}

	template <typename WndClass>
	std::ostream& logWndClass(std::ostream& os, const WndClass& wc)
	{
		Compat::LogStruct log(os);
		log << Compat::hex(wc.style)
			<< wc.lpfnWndProc
			<< wc.cbClsExtra
			<< wc.cbWndExtra
			<< wc.hInstance
			<< wc.hIcon
			<< wc.hCursor
			<< wc.hbrBackground
			<< wc.lpszMenuName
			<< wc.lpszClassName;
		if constexpr (std::is_same_v<WndClass, WNDCLASSEXA> || std::is_same_v<WndClass, WNDCLASSEXW>)
		{
			log << wc.hIconSm;
		}
		return log;
	}
}

std::ostream& operator<<(std::ostream& os, const BITMAP& bm)
{
	return Compat::LogStruct(os)
		<< bm.bmType
		<< bm.bmWidth
		<< bm.bmHeight
		<< bm.bmWidthBytes
		<< bm.bmPlanes
		<< bm.bmBitsPixel
		<< bm.bmBits;
}

std::ostream& operator<<(std::ostream& os, const BITMAPINFO& bmi)
{
	return Compat::LogStruct(os)
		<< bmi.bmiHeader
		<< Compat::array(bmi.bmiColors, 1);
}

std::ostream& operator<<(std::ostream& os, const BITMAPINFOHEADER& bmih)
{
	return Compat::LogStruct(os)
		<< bmih.biSize
		<< bmih.biWidth
		<< bmih.biHeight
		<< bmih.biPlanes
		<< bmih.biBitCount
		<< bmih.biCompression
		<< bmih.biSizeImage
		<< bmih.biXPelsPerMeter
		<< bmih.biYPelsPerMeter
		<< bmih.biClrUsed
		<< bmih.biClrImportant;
}

std::ostream& operator<<(std::ostream& os, const BLENDFUNCTION& bf)
{
	return Compat::LogStruct(os)
		<< static_cast<UINT>(bf.BlendOp)
		<< static_cast<UINT>(bf.BlendFlags)
		<< static_cast<UINT>(bf.SourceConstantAlpha)
		<< static_cast<UINT>(bf.AlphaFormat);
}

std::ostream& operator<<(std::ostream& os, const COMPAREITEMSTRUCT& cis)
{
	return Compat::LogStruct(os)
		<< cis.CtlType
		<< cis.CtlID
		<< cis.hwndItem
		<< cis.itemID1
		<< Compat::hex(cis.itemData1)
		<< cis.itemID2
		<< Compat::hex(cis.itemData2)
		<< Compat::hex(cis.dwLocaleId);
}

std::ostream& operator<<(std::ostream& os, const COPYDATASTRUCT& cds)
{
	return Compat::LogStruct(os)
		<< Compat::hex(cds.dwData)
		<< cds.cbData
		<< cds.lpData;
}

std::ostream& operator<<(std::ostream& os, const CREATESTRUCTA& cs)
{
	return logCreateStruct(os, cs);
}

std::ostream& operator<<(std::ostream& os, const CREATESTRUCTW& cs)
{
	return logCreateStruct(os, cs);
}

std::ostream& operator<<(std::ostream& os, const CWPSTRUCT& cwp)
{
	return Compat::LogStruct(os)
		<< Compat::WindowMessageStruct(cwp.hwnd, cwp.message, cwp.wParam, cwp.lParam);
}

std::ostream& operator<<(std::ostream& os, const CWPRETSTRUCT& cwrp)
{
	return Compat::LogStruct(os)
		<< Compat::WindowMessageStruct(cwrp.hwnd, cwrp.message, cwrp.wParam, cwrp.lParam)
		<< Compat::hex(cwrp.lResult);
}

std::ostream& operator<<(std::ostream& os, const DELETEITEMSTRUCT& dis)
{
	return Compat::LogStruct(os)
		<< dis.CtlType
		<< dis.CtlID
		<< dis.itemID
		<< dis.hwndItem
		<< Compat::hex(dis.itemData);
}

std::ostream& operator<<(std::ostream& os, const DEVMODEA& dm)
{
	return logDevMode(os, dm);
}

std::ostream& operator<<(std::ostream& os, const DEVMODEW& dm)
{
	return logDevMode(os, dm);
}

std::ostream& operator<<(std::ostream& os, const DRAWITEMSTRUCT& dis)
{
	return Compat::LogStruct(os)
		<< dis.CtlType
		<< dis.CtlID
		<< dis.itemID
		<< Compat::hex(dis.itemAction)
		<< Compat::hex(dis.itemState)
		<< dis.hwndItem
		<< dis.hDC
		<< dis.rcItem
		<< Compat::hex(dis.itemData);
}

std::ostream& operator<<(std::ostream& os, const GESTURENOTIFYSTRUCT& gns)
{
	return Compat::LogStruct(os)
		<< gns.cbSize
		<< Compat::hex(gns.dwFlags)
		<< gns.hwndTarget
		<< gns.ptsLocation
		<< gns.dwInstanceID;
}

std::ostream& operator<<(std::ostream& os, const HDC__& dc)
{
	os << "DC";
	HDC hdc = const_cast<HDC>(&dc);
	return Compat::LogStruct(os)
		<< static_cast<void*>(hdc)
		<< CALL_ORIG_FUNC(WindowFromDC)(hdc);
}

std::ostream& operator<<(std::ostream& os, const HELPINFO& hi)
{
	Compat::LogStruct log(os);
	log << hi.cbSize
		<< hi.iContextType
		<< hi.iCtrlId;

	if (HELPINFO_WINDOW == hi.iContextType)
	{
		log << static_cast<HWND>(hi.hItemHandle);
	}
	else
	{
		log << static_cast<HMENU>(hi.hItemHandle);
	}

	return log
		<< hi.dwContextId
		<< hi.MousePos;
}

std::ostream& operator<<(std::ostream& os, const HFONT__& font)
{
	HFONT hfont = const_cast<HFONT>(&font);
	LOGFONT lf = {};
	GetObject(hfont, sizeof(lf), &lf);
	return Compat::LogStruct(os)
		<< static_cast<void*>(hfont)
		<< lf;
}

std::ostream& operator<<(std::ostream& os, const HINSTANCE__& inst)
{
	os << "MOD";
	return Compat::LogStruct(os)
		<< static_cast<const void*>(&inst)
		<< Compat::getModulePath(const_cast<HINSTANCE>(&inst));
}

std::ostream& operator<<(std::ostream& os, const HRGN__& rgn)
{
	os << "RGN";
	HRGN hrgn = const_cast<HRGN>(&rgn);
	DWORD size = GetRegionData(hrgn, 0, nullptr);
	if (0 == size)
	{
		return os << "[]";
	}

	std::vector<unsigned char> rgnDataBuf(size);
	auto& rgnData = *reinterpret_cast<RGNDATA*>(rgnDataBuf.data());
	GetRegionData(hrgn, size, &rgnData);

	Compat::LogStream(os) << Compat::array(reinterpret_cast<RECT*>(rgnData.Buffer), rgnData.rdh.nCount);
	return os;
}

std::ostream& operator<<(std::ostream& os, const HWND__& wnd)
{
	os << "WND";
	HWND hwnd = const_cast<HWND>(&wnd);
	if (!IsWindow(hwnd))
	{
		return Compat::LogStruct(os)
			<< static_cast<void*>(hwnd)
			<< "INVALID";
	}

	char name[256] = {};
	RECT rect = {};
	GetClassName(hwnd, name, sizeof(name));
	CALL_ORIG_FUNC(GetWindowRect)(hwnd, &rect);

	return Compat::LogStruct(os)
		<< static_cast<void*>(hwnd)
		<< static_cast<void*>(GetParent(hwnd))
		<< name
		<< Compat::hex(GetClassLong(hwnd, GCL_STYLE))
		<< rect
		<< Compat::hex(CALL_ORIG_FUNC(GetWindowLongA)(hwnd, GWL_STYLE))
		<< Compat::hex(CALL_ORIG_FUNC(GetWindowLongA)(hwnd, GWL_EXSTYLE));
}

std::ostream& operator<<(std::ostream& os, const LOGFONT& lf)
{
	return Compat::LogStruct(os)
		<< lf.lfHeight
		<< lf.lfWidth
		<< lf.lfEscapement
		<< lf.lfOrientation
		<< lf.lfWeight
		<< static_cast<UINT>(lf.lfItalic)
		<< static_cast<UINT>(lf.lfUnderline)
		<< static_cast<UINT>(lf.lfStrikeOut)
		<< static_cast<UINT>(lf.lfCharSet)
		<< static_cast<UINT>(lf.lfOutPrecision)
		<< static_cast<UINT>(lf.lfClipPrecision)
		<< static_cast<UINT>(lf.lfQuality)
		<< Compat::hex<UINT>(lf.lfPitchAndFamily)
		<< lf.lfFaceName;
}

std::ostream& operator<<(std::ostream& os, const MDICREATESTRUCTA& mcs)
{
	return logMdiCreateStruct(os, mcs);
}

std::ostream& operator<<(std::ostream& os, const MDICREATESTRUCTW& mcs)
{
	return logMdiCreateStruct(os, mcs);
}

std::ostream& operator<<(std::ostream& os, const MDINEXTMENU& mnm)
{
	return Compat::LogStruct(os)
		<< mnm.hmenuIn
		<< mnm.hmenuNext
		<< mnm.hwndNext;
}

std::ostream& operator<<(std::ostream& os, const MEASUREITEMSTRUCT& mis)
{
	return Compat::LogStruct(os)
		<< mis.CtlType
		<< mis.CtlID
		<< mis.itemID
		<< mis.itemWidth
		<< mis.itemHeight
		<< Compat::hex(mis.itemData);
}

std::ostream& operator<<(std::ostream& os, const MEMORYSTATUS& ms)
{
	return Compat::LogStruct(os)
		<< ms.dwLength
		<< ms.dwMemoryLoad
		<< ms.dwTotalPhys
		<< ms.dwAvailPhys
		<< ms.dwTotalPageFile
		<< ms.dwAvailPageFile
		<< ms.dwTotalVirtual
		<< ms.dwAvailVirtual;
}

std::ostream& operator<<(std::ostream& os, const MENUGETOBJECTINFO& mgoi)
{
	return Compat::LogStruct(os)
		<< Compat::hex(mgoi.dwFlags)
		<< mgoi.uPos
		<< mgoi.hmenu
		<< static_cast<GUID*>(mgoi.riid)
		<< mgoi.pvObj;
}

std::ostream& operator<<(std::ostream& os, const MINMAXINFO& mmi)
{
	return Compat::LogStruct(os)
		<< mmi.ptReserved
		<< mmi.ptMaxSize
		<< mmi.ptMaxPosition
		<< mmi.ptMinTrackSize
		<< mmi.ptMaxTrackSize;
}

std::ostream& operator<<(std::ostream& os, const MSG& msg)
{
	return Compat::LogStruct(os)
		<< Compat::WindowMessageStruct(msg.hwnd, msg.message, msg.wParam, msg.lParam)
		<< msg.time
		<< msg.pt;
}

std::ostream& operator<<(std::ostream& os, const NCCALCSIZE_PARAMS& nccs)
{
	return Compat::LogStruct(os)
		<< Compat::array(nccs.rgrc, sizeof(nccs.rgrc) / sizeof(nccs.rgrc[0]))
		<< nccs.lppos;
}

std::ostream& operator<<(std::ostream& os, const NMHDR& nm)
{
	return Compat::LogStruct(os)
		<< nm.hwndFrom
		<< nm.idFrom
		<< Compat::hex(nm.code);
}

std::ostream& operator<<(std::ostream& os, const OSVERSIONINFOA& vi)
{
	return logOsVersionInfo<OSVERSIONINFOEXA>(os, vi);
}

std::ostream& operator<<(std::ostream& os, const OSVERSIONINFOW& vi)
{
	return logOsVersionInfo<OSVERSIONINFOEXW>(os, vi);
}

std::ostream& operator<<(std::ostream& os, const OSVERSIONINFOEXA& vi)
{
	return logOsVersionInfoEx(os, vi);
}

std::ostream& operator<<(std::ostream& os, const OSVERSIONINFOEXW& vi)
{
	return logOsVersionInfoEx(os, vi);
}

std::ostream& operator<<(std::ostream& os, const PALETTEENTRY& pe)
{
	auto fill = os.fill('0');
	return os << std::setw(8) << std::hex << *reinterpret_cast<const DWORD*>(&pe) << std::dec << std::setfill(fill);
}

std::ostream& operator<<(std::ostream& os, const POINT& p)
{
	return Compat::LogStruct(os)
		<< p.x
		<< p.y;
}

std::ostream& operator<<(std::ostream& os, const POINTS& p)
{
	return Compat::LogStruct(os)
		<< p.x
		<< p.y;
}

std::ostream& operator<<(std::ostream& os, const RAWINPUTDEVICE& input)
{
	return Compat::LogStruct(os)
		<< Compat::hex(input.usUsagePage)
		<< Compat::hex(input.usUsage)
		<< Compat::hex(input.dwFlags)
		<< input.hwndTarget;
}

std::ostream& operator<<(std::ostream& os, const RECT& rect)
{
	return Compat::LogStruct(os)
		<< rect.left
		<< rect.top
		<< rect.right
		<< rect.bottom;
}

std::ostream& operator<<(std::ostream& os, const SIZE& size)
{
	return Compat::LogStruct(os)
		<< size.cx
		<< size.cy;
}

std::ostream& operator<<(std::ostream& os, const STYLESTRUCT& ss)
{
	return Compat::LogStruct(os)
		<< Compat::hex(ss.styleOld)
		<< Compat::hex(ss.styleNew);
}

std::ostream& operator<<(std::ostream& os, const TITLEBARINFOEX& tbi)
{
	return Compat::LogStruct(os)
		<< tbi.cbSize
		<< tbi.rcTitleBar
		<< Compat::array(tbi.rgstate, sizeof(tbi.rgstate) / sizeof(tbi.rgstate[0]))
		<< Compat::array(tbi.rgrect, sizeof(tbi.rgrect) / sizeof(tbi.rgrect[0]));
}

std::ostream& operator<<(std::ostream& os, const TOUCH_HIT_TESTING_INPUT& thti)
{
	return Compat::LogStruct(os)
		<< thti.pointerId
		<< thti.point
		<< thti.boundingBox
		<< thti.nonOccludedBoundingBox
		<< thti.orientation;
}

std::ostream& operator<<(std::ostream& os, const WINDOWPOS& wp)
{
	return Compat::LogStruct(os)
		<< wp.hwnd
		<< wp.hwndInsertAfter
		<< wp.x
		<< wp.y
		<< wp.cx
		<< wp.cy
		<< Compat::hex(wp.flags);
}

std::ostream& operator<<(std::ostream& os, const WNDCLASSA& wc)
{
	return logWndClass(os, wc);
}

std::ostream& operator<<(std::ostream& os, const WNDCLASSW& wc)
{
	return logWndClass(os, wc);
}

std::ostream& operator<<(std::ostream& os, const WNDCLASSEXA& wc)
{
	return logWndClass(os, wc);
}

std::ostream& operator<<(std::ostream& os, const WNDCLASSEXW& wc)
{
	return logWndClass(os, wc);
}

namespace Compat
{
	LogStream operator<<(LogStream os, WindowMessage msg)
	{
#define LOG_WM_CASE(msg) case msg: return os << #msg;
		switch (msg.msg)
		{
			LOG_WM_CASE(WM_NULL);
			LOG_WM_CASE(WM_CREATE);
			LOG_WM_CASE(WM_DESTROY);
			LOG_WM_CASE(WM_MOVE);
			LOG_WM_CASE(WM_SIZE);
			LOG_WM_CASE(WM_ACTIVATE);
			LOG_WM_CASE(WM_SETFOCUS);
			LOG_WM_CASE(WM_KILLFOCUS);
			LOG_WM_CASE(WM_ENABLE);
			LOG_WM_CASE(WM_SETREDRAW);
			LOG_WM_CASE(WM_SETTEXT);
			LOG_WM_CASE(WM_GETTEXT);
			LOG_WM_CASE(WM_GETTEXTLENGTH);
			LOG_WM_CASE(WM_PAINT);
			LOG_WM_CASE(WM_CLOSE);
			LOG_WM_CASE(WM_QUERYENDSESSION);
			LOG_WM_CASE(WM_QUERYOPEN);
			LOG_WM_CASE(WM_ENDSESSION);
			LOG_WM_CASE(WM_QUIT);
			LOG_WM_CASE(WM_ERASEBKGND);
			LOG_WM_CASE(WM_SYSCOLORCHANGE);
			LOG_WM_CASE(WM_SHOWWINDOW);
			LOG_WM_CASE(WM_SETTINGCHANGE);
			LOG_WM_CASE(WM_DEVMODECHANGE);
			LOG_WM_CASE(WM_ACTIVATEAPP);
			LOG_WM_CASE(WM_FONTCHANGE);
			LOG_WM_CASE(WM_TIMECHANGE);
			LOG_WM_CASE(WM_CANCELMODE);
			LOG_WM_CASE(WM_SETCURSOR);
			LOG_WM_CASE(WM_MOUSEACTIVATE);
			LOG_WM_CASE(WM_CHILDACTIVATE);
			LOG_WM_CASE(WM_QUEUESYNC);
			LOG_WM_CASE(WM_GETMINMAXINFO);
			LOG_WM_CASE(WM_PAINTICON);
			LOG_WM_CASE(WM_ICONERASEBKGND);
			LOG_WM_CASE(WM_NEXTDLGCTL);
			LOG_WM_CASE(WM_SPOOLERSTATUS);
			LOG_WM_CASE(WM_DRAWITEM);
			LOG_WM_CASE(WM_MEASUREITEM);
			LOG_WM_CASE(WM_DELETEITEM);
			LOG_WM_CASE(WM_VKEYTOITEM);
			LOG_WM_CASE(WM_CHARTOITEM);
			LOG_WM_CASE(WM_SETFONT);
			LOG_WM_CASE(WM_GETFONT);
			LOG_WM_CASE(WM_SETHOTKEY);
			LOG_WM_CASE(WM_GETHOTKEY);
			LOG_WM_CASE(WM_QUERYDRAGICON);
			LOG_WM_CASE(WM_COMPAREITEM);
			LOG_WM_CASE(WM_GETOBJECT);
			LOG_WM_CASE(WM_COMPACTING);
			LOG_WM_CASE(WM_COMMNOTIFY);
			LOG_WM_CASE(WM_WINDOWPOSCHANGING);
			LOG_WM_CASE(WM_WINDOWPOSCHANGED);
			LOG_WM_CASE(WM_POWER);
			LOG_WM_CASE(WM_COPYDATA);
			LOG_WM_CASE(WM_CANCELJOURNAL);
			LOG_WM_CASE(WM_NOTIFY);
			LOG_WM_CASE(WM_INPUTLANGCHANGEREQUEST);
			LOG_WM_CASE(WM_INPUTLANGCHANGE);
			LOG_WM_CASE(WM_TCARD);
			LOG_WM_CASE(WM_HELP);
			LOG_WM_CASE(WM_USERCHANGED);
			LOG_WM_CASE(WM_NOTIFYFORMAT);
			LOG_WM_CASE(WM_CONTEXTMENU);
			LOG_WM_CASE(WM_STYLECHANGING);
			LOG_WM_CASE(WM_STYLECHANGED);
			LOG_WM_CASE(WM_DISPLAYCHANGE);
			LOG_WM_CASE(WM_GETICON);
			LOG_WM_CASE(WM_SETICON);
			LOG_WM_CASE(WM_NCCREATE);
			LOG_WM_CASE(WM_NCDESTROY);
			LOG_WM_CASE(WM_NCCALCSIZE);
			LOG_WM_CASE(WM_NCHITTEST);
			LOG_WM_CASE(WM_NCPAINT);
			LOG_WM_CASE(WM_NCACTIVATE);
			LOG_WM_CASE(WM_GETDLGCODE);
			LOG_WM_CASE(WM_SYNCPAINT);
			LOG_WM_CASE(WM_NCMOUSEMOVE);
			LOG_WM_CASE(WM_NCLBUTTONDOWN);
			LOG_WM_CASE(WM_NCLBUTTONUP);
			LOG_WM_CASE(WM_NCLBUTTONDBLCLK);
			LOG_WM_CASE(WM_NCRBUTTONDOWN);
			LOG_WM_CASE(WM_NCRBUTTONUP);
			LOG_WM_CASE(WM_NCRBUTTONDBLCLK);
			LOG_WM_CASE(WM_NCMBUTTONDOWN);
			LOG_WM_CASE(WM_NCMBUTTONUP);
			LOG_WM_CASE(WM_NCMBUTTONDBLCLK);
			LOG_WM_CASE(WM_NCXBUTTONDOWN);
			LOG_WM_CASE(WM_NCXBUTTONUP);
			LOG_WM_CASE(WM_NCXBUTTONDBLCLK);
			LOG_WM_CASE(WM_INPUT_DEVICE_CHANGE);
			LOG_WM_CASE(WM_INPUT);
			LOG_WM_CASE(WM_KEYDOWN);
			LOG_WM_CASE(WM_KEYUP);
			LOG_WM_CASE(WM_CHAR);
			LOG_WM_CASE(WM_DEADCHAR);
			LOG_WM_CASE(WM_SYSKEYDOWN);
			LOG_WM_CASE(WM_SYSKEYUP);
			LOG_WM_CASE(WM_SYSCHAR);
			LOG_WM_CASE(WM_SYSDEADCHAR);
			LOG_WM_CASE(WM_UNICHAR);
			LOG_WM_CASE(WM_IME_STARTCOMPOSITION);
			LOG_WM_CASE(WM_IME_ENDCOMPOSITION);
			LOG_WM_CASE(WM_IME_COMPOSITION);
			LOG_WM_CASE(WM_INITDIALOG);
			LOG_WM_CASE(WM_COMMAND);
			LOG_WM_CASE(WM_SYSCOMMAND);
			LOG_WM_CASE(WM_TIMER);
			LOG_WM_CASE(WM_HSCROLL);
			LOG_WM_CASE(WM_VSCROLL);
			LOG_WM_CASE(WM_INITMENU);
			LOG_WM_CASE(WM_INITMENUPOPUP);
			LOG_WM_CASE(WM_GESTURE);
			LOG_WM_CASE(WM_GESTURENOTIFY);
			LOG_WM_CASE(WM_MENUSELECT);
			LOG_WM_CASE(WM_MENUCHAR);
			LOG_WM_CASE(WM_ENTERIDLE);
			LOG_WM_CASE(WM_MENURBUTTONUP);
			LOG_WM_CASE(WM_MENUDRAG);
			LOG_WM_CASE(WM_MENUGETOBJECT);
			LOG_WM_CASE(WM_UNINITMENUPOPUP);
			LOG_WM_CASE(WM_MENUCOMMAND);
			LOG_WM_CASE(WM_CHANGEUISTATE);
			LOG_WM_CASE(WM_UPDATEUISTATE);
			LOG_WM_CASE(WM_QUERYUISTATE);
			LOG_WM_CASE(WM_CTLCOLORMSGBOX);
			LOG_WM_CASE(WM_CTLCOLOREDIT);
			LOG_WM_CASE(WM_CTLCOLORLISTBOX);
			LOG_WM_CASE(WM_CTLCOLORBTN);
			LOG_WM_CASE(WM_CTLCOLORDLG);
			LOG_WM_CASE(WM_CTLCOLORSCROLLBAR);
			LOG_WM_CASE(WM_CTLCOLORSTATIC);
			LOG_WM_CASE(MN_GETHMENU);
			LOG_WM_CASE(WM_MOUSEMOVE);
			LOG_WM_CASE(WM_LBUTTONDOWN);
			LOG_WM_CASE(WM_LBUTTONUP);
			LOG_WM_CASE(WM_LBUTTONDBLCLK);
			LOG_WM_CASE(WM_RBUTTONDOWN);
			LOG_WM_CASE(WM_RBUTTONUP);
			LOG_WM_CASE(WM_RBUTTONDBLCLK);
			LOG_WM_CASE(WM_MBUTTONDOWN);
			LOG_WM_CASE(WM_MBUTTONUP);
			LOG_WM_CASE(WM_MBUTTONDBLCLK);
			LOG_WM_CASE(WM_MOUSEWHEEL);
			LOG_WM_CASE(WM_XBUTTONDOWN);
			LOG_WM_CASE(WM_XBUTTONUP);
			LOG_WM_CASE(WM_XBUTTONDBLCLK);
			LOG_WM_CASE(WM_MOUSEHWHEEL);
			LOG_WM_CASE(WM_PARENTNOTIFY);
			LOG_WM_CASE(WM_ENTERMENULOOP);
			LOG_WM_CASE(WM_EXITMENULOOP);
			LOG_WM_CASE(WM_NEXTMENU);
			LOG_WM_CASE(WM_SIZING);
			LOG_WM_CASE(WM_CAPTURECHANGED);
			LOG_WM_CASE(WM_MOVING);
			LOG_WM_CASE(WM_POWERBROADCAST);
			LOG_WM_CASE(WM_DEVICECHANGE);
			LOG_WM_CASE(WM_MDICREATE);
			LOG_WM_CASE(WM_MDIDESTROY);
			LOG_WM_CASE(WM_MDIACTIVATE);
			LOG_WM_CASE(WM_MDIRESTORE);
			LOG_WM_CASE(WM_MDINEXT);
			LOG_WM_CASE(WM_MDIMAXIMIZE);
			LOG_WM_CASE(WM_MDITILE);
			LOG_WM_CASE(WM_MDICASCADE);
			LOG_WM_CASE(WM_MDIICONARRANGE);
			LOG_WM_CASE(WM_MDIGETACTIVE);
			LOG_WM_CASE(WM_MDISETMENU);
			LOG_WM_CASE(WM_ENTERSIZEMOVE);
			LOG_WM_CASE(WM_EXITSIZEMOVE);
			LOG_WM_CASE(WM_DROPFILES);
			LOG_WM_CASE(WM_MDIREFRESHMENU);
			LOG_WM_CASE(WM_POINTERDEVICECHANGE);
			LOG_WM_CASE(WM_POINTERDEVICEINRANGE);
			LOG_WM_CASE(WM_POINTERDEVICEOUTOFRANGE);
			LOG_WM_CASE(WM_TOUCH);
			LOG_WM_CASE(WM_NCPOINTERUPDATE);
			LOG_WM_CASE(WM_NCPOINTERDOWN);
			LOG_WM_CASE(WM_NCPOINTERUP);
			LOG_WM_CASE(WM_POINTERUPDATE);
			LOG_WM_CASE(WM_POINTERDOWN);
			LOG_WM_CASE(WM_POINTERUP);
			LOG_WM_CASE(WM_POINTERENTER);
			LOG_WM_CASE(WM_POINTERLEAVE);
			LOG_WM_CASE(WM_POINTERACTIVATE);
			LOG_WM_CASE(WM_POINTERCAPTURECHANGED);
			LOG_WM_CASE(WM_TOUCHHITTESTING);
			LOG_WM_CASE(WM_POINTERWHEEL);
			LOG_WM_CASE(WM_POINTERHWHEEL);
			LOG_WM_CASE(DM_POINTERHITTEST);
			LOG_WM_CASE(WM_POINTERROUTEDTO);
			LOG_WM_CASE(WM_POINTERROUTEDAWAY);
			LOG_WM_CASE(WM_POINTERROUTEDRELEASED);
			LOG_WM_CASE(WM_IME_SETCONTEXT);
			LOG_WM_CASE(WM_IME_NOTIFY);
			LOG_WM_CASE(WM_IME_CONTROL);
			LOG_WM_CASE(WM_IME_COMPOSITIONFULL);
			LOG_WM_CASE(WM_IME_SELECT);
			LOG_WM_CASE(WM_IME_CHAR);
			LOG_WM_CASE(WM_IME_REQUEST);
			LOG_WM_CASE(WM_IME_KEYDOWN);
			LOG_WM_CASE(WM_IME_KEYUP);
			LOG_WM_CASE(WM_NCMOUSEHOVER);
			LOG_WM_CASE(WM_MOUSEHOVER);
			LOG_WM_CASE(WM_NCMOUSELEAVE);
			LOG_WM_CASE(WM_MOUSELEAVE);
			LOG_WM_CASE(WM_WTSSESSION_CHANGE);
			LOG_WM_CASE(WM_DPICHANGED);
			LOG_WM_CASE(WM_DPICHANGED_BEFOREPARENT);
			LOG_WM_CASE(WM_DPICHANGED_AFTERPARENT);
			LOG_WM_CASE(WM_GETDPISCALEDSIZE);
			LOG_WM_CASE(WM_CUT);
			LOG_WM_CASE(WM_COPY);
			LOG_WM_CASE(WM_PASTE);
			LOG_WM_CASE(WM_CLEAR);
			LOG_WM_CASE(WM_UNDO);
			LOG_WM_CASE(WM_RENDERFORMAT);
			LOG_WM_CASE(WM_RENDERALLFORMATS);
			LOG_WM_CASE(WM_DESTROYCLIPBOARD);
			LOG_WM_CASE(WM_DRAWCLIPBOARD);
			LOG_WM_CASE(WM_PAINTCLIPBOARD);
			LOG_WM_CASE(WM_VSCROLLCLIPBOARD);
			LOG_WM_CASE(WM_SIZECLIPBOARD);
			LOG_WM_CASE(WM_ASKCBFORMATNAME);
			LOG_WM_CASE(WM_CHANGECBCHAIN);
			LOG_WM_CASE(WM_HSCROLLCLIPBOARD);
			LOG_WM_CASE(WM_QUERYNEWPALETTE);
			LOG_WM_CASE(WM_PALETTEISCHANGING);
			LOG_WM_CASE(WM_PALETTECHANGED);
			LOG_WM_CASE(WM_HOTKEY);
			LOG_WM_CASE(WM_PRINT);
			LOG_WM_CASE(WM_PRINTCLIENT);
			LOG_WM_CASE(WM_APPCOMMAND);
			LOG_WM_CASE(WM_THEMECHANGED);
			LOG_WM_CASE(WM_CLIPBOARDUPDATE);
			LOG_WM_CASE(WM_DWMCOMPOSITIONCHANGED);
			LOG_WM_CASE(WM_DWMNCRENDERINGCHANGED);
			LOG_WM_CASE(WM_DWMCOLORIZATIONCOLORCHANGED);
			LOG_WM_CASE(WM_DWMWINDOWMAXIMIZEDCHANGE);
			LOG_WM_CASE(WM_DWMSENDICONICTHUMBNAIL);
			LOG_WM_CASE(WM_DWMSENDICONICLIVEPREVIEWBITMAP);
			LOG_WM_CASE(WM_GETTITLEBARINFOEX);
		};
#undef LOG_WM_CASE

		return os << "WM_" << std::hex << msg.msg << std::dec;
	}

	LogStream operator<<(LogStream os, WindowMessage16 msg)
	{
		return os << WindowMessage(msg.msg);
	}

	LogStream operator<<(LogStream os, WindowMessageStruct wm)
	{
		Compat::LogStruct log(os);
		log << wm.hwnd << wm.msg;

#define LOG_PARAM_CASE_1(param, msg, ...) \
	case msg: \
		static_assert(sizeof(__VA_ARGS__) == sizeof(param)); \
		log << ParamConverter<__VA_ARGS__>::convert(param); \
		break

#define LOG_PARAM_CASE_2(param, msg, TypeA, TypeW) \
	case msg: \
		if (IsWindowUnicode(wm.hwnd)) \
			log << ParamConverter<TypeW>::convert(param); \
		else \
			log << ParamConverter<TypeA>::convert(param); \
		break;

#define LOG_WPARAM_CASE_1(msg, ...) LOG_PARAM_CASE_1(wm.wParam, msg, __VA_ARGS__)
#define LOG_WPARAM_CASE_2(msg, TypeA, TypeW) LOG_PARAM_CASE_2(wm.wParam, msg, TypeA, TypeW)

		switch (wm.msg.msg)
		{
			LOG_WPARAM_CASE_1(WM_ACTIVATE, std::pair<WORD, WORD>);
			LOG_WPARAM_CASE_1(WM_APPCOMMAND, HWND);
			LOG_WPARAM_CASE_1(WM_ASKCBFORMATNAME, DWORD);
			LOG_WPARAM_CASE_1(WM_CHANGECBCHAIN, HWND);
			LOG_WPARAM_CASE_1(WM_CHANGEUISTATE, std::pair<WORD, Hex<WORD>>);
			LOG_WPARAM_CASE_1(WM_CHARTOITEM, std::pair<WORD, WORD>);
			LOG_WPARAM_CASE_1(WM_CTLCOLORMSGBOX, HDC);
			LOG_WPARAM_CASE_1(WM_CTLCOLOREDIT, HDC);
			LOG_WPARAM_CASE_1(WM_CTLCOLORLISTBOX, HDC);
			LOG_WPARAM_CASE_1(WM_CTLCOLORBTN, HDC);
			LOG_WPARAM_CASE_1(WM_CTLCOLORDLG, HDC);
			LOG_WPARAM_CASE_1(WM_CTLCOLORSCROLLBAR, HDC);
			LOG_WPARAM_CASE_1(WM_CTLCOLORSTATIC, HDC);
			LOG_WPARAM_CASE_1(WM_COMMAND, std::pair<Hex<WORD>, WORD>);
			LOG_WPARAM_CASE_1(WM_CONTEXTMENU, HWND);
			LOG_WPARAM_CASE_1(WM_COPYDATA, HWND);
			LOG_WPARAM_CASE_1(WM_DISPLAYCHANGE, INT);
			LOG_WPARAM_CASE_1(WM_DPICHANGED, std::pair<WORD, WORD>);
			LOG_WPARAM_CASE_1(WM_DRAWITEM, HWND);
			LOG_WPARAM_CASE_1(WM_ERASEBKGND, HDC);
			LOG_WPARAM_CASE_1(WM_GETDPISCALEDSIZE, DWORD);
			LOG_WPARAM_CASE_1(WM_GETTEXT, DWORD);
			LOG_WPARAM_CASE_1(WM_HOTKEY, INT);
			LOG_WPARAM_CASE_1(WM_HSCROLL, std::pair<WORD, WORD>);
			LOG_WPARAM_CASE_1(WM_HSCROLLCLIPBOARD, HWND);
			LOG_WPARAM_CASE_1(WM_ICONERASEBKGND, HDC);
			LOG_WPARAM_CASE_1(WM_INITDIALOG, HWND);
			LOG_WPARAM_CASE_1(WM_KILLFOCUS, HWND);
			LOG_WPARAM_CASE_1(WM_MDIACTIVATE, HWND);
			LOG_WPARAM_CASE_1(WM_MDIDESTROY, HWND);
			LOG_WPARAM_CASE_1(WM_MDIMAXIMIZE, HWND);
			LOG_WPARAM_CASE_1(WM_MDINEXT, HWND);
			LOG_WPARAM_CASE_1(WM_MDIRESTORE, HWND);
			LOG_WPARAM_CASE_1(WM_MENUCHAR, std::pair<Hex<WORD>, Hex<WORD>>);
			LOG_WPARAM_CASE_1(WM_MENUCOMMAND, DWORD);
			LOG_WPARAM_CASE_1(WM_MENUDRAG, DWORD);
			LOG_WPARAM_CASE_1(WM_MENURBUTTONUP, DWORD);
			LOG_WPARAM_CASE_1(WM_MENUSELECT, std::pair<WORD, Hex<WORD>>);
			LOG_WPARAM_CASE_1(WM_MOUSEACTIVATE, HWND);
			LOG_WPARAM_CASE_1(WM_MOUSEHWHEEL, std::pair<Hex<WORD>, SHORT>);
			LOG_WPARAM_CASE_1(WM_MOUSEWHEEL, std::pair<Hex<WORD>, SHORT>);
			LOG_WPARAM_CASE_1(WM_NCLBUTTONDBLCLK, INT);
			LOG_WPARAM_CASE_1(WM_NCLBUTTONDOWN, INT);
			LOG_WPARAM_CASE_1(WM_NCLBUTTONUP, INT);
			LOG_WPARAM_CASE_1(WM_NCMBUTTONDBLCLK, INT);
			LOG_WPARAM_CASE_1(WM_NCMBUTTONDOWN, INT);
			LOG_WPARAM_CASE_1(WM_NCMBUTTONUP, INT);
			LOG_WPARAM_CASE_1(WM_NCMOUSEHOVER, INT);
			LOG_WPARAM_CASE_1(WM_NCMOUSEMOVE, INT);
			LOG_WPARAM_CASE_1(WM_NCPAINT, HRGN);
			LOG_WPARAM_CASE_1(WM_NCPOINTERDOWN, std::pair<WORD, SHORT>);
			LOG_WPARAM_CASE_1(WM_NCPOINTERUP, std::pair<WORD, SHORT>);
			LOG_WPARAM_CASE_1(WM_NCPOINTERUPDATE, std::pair<WORD, SHORT>);
			LOG_WPARAM_CASE_1(WM_NCXBUTTONDBLCLK, std::pair<Hex<WORD>, WORD>);
			LOG_WPARAM_CASE_1(WM_NCXBUTTONDOWN, std::pair<Hex<WORD>, WORD>);
			LOG_WPARAM_CASE_1(WM_NCXBUTTONUP, std::pair<Hex<WORD>, WORD>);
			LOG_WPARAM_CASE_1(WM_NOTIFYFORMAT, HWND);
			LOG_WPARAM_CASE_1(WM_PALETTECHANGED, HWND);
			LOG_WPARAM_CASE_1(WM_PALETTEISCHANGING, HWND);
			LOG_WPARAM_CASE_1(WM_PARENTNOTIFY, std::pair<WindowMessage16, Hex<WORD>>);
			LOG_WPARAM_CASE_1(WM_POINTERACTIVATE, std::pair<WORD, SHORT>);
			LOG_WPARAM_CASE_1(WM_POINTERDOWN, std::pair<WORD, Hex<WORD>>);
			LOG_WPARAM_CASE_1(WM_POINTERENTER, std::pair<WORD, Hex<WORD>>);
			LOG_WPARAM_CASE_1(WM_POINTERHWHEEL, std::pair<WORD, SHORT>);
			LOG_WPARAM_CASE_1(WM_POINTERLEAVE, std::pair<WORD, Hex<WORD>>);
			LOG_WPARAM_CASE_1(WM_POINTERUP, std::pair<WORD, Hex<WORD>>);
			LOG_WPARAM_CASE_1(WM_POINTERUPDATE, std::pair<WORD, Hex<WORD>>);
			LOG_WPARAM_CASE_1(WM_POINTERWHEEL, std::pair<WORD, SHORT>);
			LOG_WPARAM_CASE_1(WM_PRINT, HDC);
			LOG_WPARAM_CASE_1(WM_PRINTCLIENT, HDC);
			LOG_WPARAM_CASE_1(WM_SETFOCUS, HWND);
			LOG_WPARAM_CASE_1(WM_SETFONT, HFONT);
			LOG_WPARAM_CASE_1(WM_SETHOTKEY, std::pair<Hex<WORD>, Hex<WORD>>);
			LOG_WPARAM_CASE_1(WM_SETTEXT, DWORD);
			LOG_WPARAM_CASE_1(WM_SIZECLIPBOARD, HWND);
			LOG_WPARAM_CASE_1(WM_STYLECHANGED, INT);
			LOG_WPARAM_CASE_1(WM_STYLECHANGING, INT);
			LOG_WPARAM_CASE_1(WM_UPDATEUISTATE, std::pair<WORD, Hex<WORD>>);
			LOG_WPARAM_CASE_1(WM_VKEYTOITEM, std::pair<Hex<WORD>, WORD>);
			LOG_WPARAM_CASE_1(WM_VSCROLL, std::pair<WORD, WORD>);
			LOG_WPARAM_CASE_1(WM_VSCROLLCLIPBOARD, HWND);
			LOG_WPARAM_CASE_1(WM_XBUTTONDBLCLK, std::pair<Hex<WORD>, WORD>);
			LOG_WPARAM_CASE_1(WM_XBUTTONDOWN, std::pair<Hex<WORD>, WORD>);
			LOG_WPARAM_CASE_1(WM_XBUTTONUP, std::pair<Hex<WORD>, WORD>);

		case WM_NEXTDLGCTL:
			if (wm.lParam)
			{
				log << reinterpret_cast<HWND>(wm.wParam);
			}
			else
			{
				log << wm.wParam;
			}
			break;

		default:
			log << hex(wm.wParam);
			break;
		}

#undef LOG_WPARAM_CASE_1
#undef LOG_WPARAM_CASE_2

#define LOG_LPARAM_CASE_1(msg, ...) LOG_PARAM_CASE_1(wm.lParam, msg, __VA_ARGS__)
#define LOG_LPARAM_CASE_2(msg, TypeA, TypeW) LOG_PARAM_CASE_2(wm.lParam, msg, TypeA, TypeW)

		switch (wm.msg.msg)
		{
			LOG_LPARAM_CASE_1(WM_ACTIVATE, HWND);
			LOG_LPARAM_CASE_2(WM_ASKCBFORMATNAME, Out<LPCSTR>, Out<LPCWSTR>);
			LOG_LPARAM_CASE_1(WM_CAPTURECHANGED, HWND);
			LOG_LPARAM_CASE_1(WM_CHANGECBCHAIN, HWND);
			LOG_LPARAM_CASE_1(WM_CHARTOITEM, HWND);
			LOG_LPARAM_CASE_1(WM_CTLCOLORMSGBOX, HWND);
			LOG_LPARAM_CASE_1(WM_CTLCOLOREDIT, HWND);
			LOG_LPARAM_CASE_1(WM_CTLCOLORLISTBOX, HWND);
			LOG_LPARAM_CASE_1(WM_CTLCOLORBTN, HWND);
			LOG_LPARAM_CASE_1(WM_CTLCOLORDLG, HWND);
			LOG_LPARAM_CASE_1(WM_CTLCOLORSCROLLBAR, HWND);
			LOG_LPARAM_CASE_1(WM_CTLCOLORSTATIC, HWND);
			LOG_LPARAM_CASE_1(WM_COMMAND, HWND);
			LOG_LPARAM_CASE_1(WM_COMPAREITEM, COMPAREITEMSTRUCT*);
			LOG_LPARAM_CASE_1(WM_CONTEXTMENU, POINTS);
			LOG_LPARAM_CASE_1(WM_COPYDATA, COPYDATASTRUCT*);
			LOG_LPARAM_CASE_2(WM_CREATE, CREATESTRUCTA*, CREATESTRUCTW*);
			LOG_LPARAM_CASE_2(WM_DEVMODECHANGE, LPCSTR, LPCWSTR);
			LOG_LPARAM_CASE_1(WM_DELETEITEM, DELETEITEMSTRUCT*);
			LOG_LPARAM_CASE_1(WM_DISPLAYCHANGE, std::pair<WORD, WORD>);
			LOG_LPARAM_CASE_1(WM_DPICHANGED, RECT*);
			LOG_LPARAM_CASE_1(WM_DRAWITEM, DRAWITEMSTRUCT*);
			LOG_LPARAM_CASE_1(WM_DWMSENDICONICTHUMBNAIL, std::pair<WORD, WORD>);
			LOG_LPARAM_CASE_1(WM_ENTERIDLE, HWND);
			LOG_LPARAM_CASE_1(WM_GESTURENOTIFY, GESTURENOTIFYSTRUCT*);
			LOG_LPARAM_CASE_1(WM_GETDLGCODE, MSG*);
			LOG_LPARAM_CASE_1(WM_GETDPISCALEDSIZE, SIZE*);
			LOG_LPARAM_CASE_1(WM_GETICON, DWORD);
			LOG_LPARAM_CASE_1(WM_GETMINMAXINFO, MINMAXINFO*);
			LOG_LPARAM_CASE_2(WM_GETTEXT, Out<LPCSTR>, Out<LPCWSTR>);
			LOG_LPARAM_CASE_1(WM_HELP, HELPINFO*);
			LOG_LPARAM_CASE_1(WM_HOTKEY, std::pair<Hex<WORD>, Hex<WORD>>);
			LOG_LPARAM_CASE_1(WM_HSCROLL, HWND);
			LOG_LPARAM_CASE_1(WM_HSCROLLCLIPBOARD, std::pair<WORD, WORD>);
			LOG_LPARAM_CASE_1(WM_INITMENUPOPUP, std::pair<WORD, WORD>);
			LOG_LPARAM_CASE_1(WM_LBUTTONDBLCLK, POINTS);
			LOG_LPARAM_CASE_1(WM_LBUTTONDOWN, POINTS);
			LOG_LPARAM_CASE_1(WM_LBUTTONUP, POINTS);
			LOG_LPARAM_CASE_1(WM_MBUTTONDBLCLK, POINTS);
			LOG_LPARAM_CASE_1(WM_MBUTTONDOWN, POINTS);
			LOG_LPARAM_CASE_1(WM_MBUTTONUP, POINTS);
			LOG_LPARAM_CASE_2(WM_MDICREATE, MDICREATESTRUCTA*, MDICREATESTRUCTW*);
			LOG_LPARAM_CASE_1(WM_MDIGETACTIVE, BOOL*);
			LOG_LPARAM_CASE_1(WM_MEASUREITEM, MEASUREITEMSTRUCT*);
			LOG_LPARAM_CASE_1(WM_MENUGETOBJECT, MENUGETOBJECTINFO*);
			LOG_LPARAM_CASE_1(WM_MOUSEACTIVATE, std::pair<SHORT, Hex<WORD>>);
			LOG_LPARAM_CASE_1(WM_MOUSEHOVER, POINTS);
			LOG_LPARAM_CASE_1(WM_MOUSEHWHEEL, POINTS);
			LOG_LPARAM_CASE_1(WM_MOUSEMOVE, POINTS);
			LOG_LPARAM_CASE_1(WM_MOUSEWHEEL, POINTS);
			LOG_LPARAM_CASE_1(WM_MOVE, POINTS);
			LOG_LPARAM_CASE_1(WM_MOVING, RECT*);
			LOG_LPARAM_CASE_2(WM_NCCREATE, CREATESTRUCTA*, CREATESTRUCTW*);
			LOG_LPARAM_CASE_1(WM_NCHITTEST, POINTS);
			LOG_LPARAM_CASE_1(WM_NCLBUTTONDBLCLK, POINTS);
			LOG_LPARAM_CASE_1(WM_NCLBUTTONDOWN, POINTS);
			LOG_LPARAM_CASE_1(WM_NCLBUTTONUP, POINTS);
			LOG_LPARAM_CASE_1(WM_NCMBUTTONDBLCLK, POINTS);
			LOG_LPARAM_CASE_1(WM_NCMBUTTONDOWN, POINTS);
			LOG_LPARAM_CASE_1(WM_NCMBUTTONUP, POINTS);
			LOG_LPARAM_CASE_1(WM_NCMOUSEHOVER, POINTS);
			LOG_LPARAM_CASE_1(WM_NCMOUSEMOVE, POINTS);
			LOG_LPARAM_CASE_1(WM_NCPOINTERDOWN, POINTS);
			LOG_LPARAM_CASE_1(WM_NCPOINTERUP, POINTS);
			LOG_LPARAM_CASE_1(WM_NCPOINTERUPDATE, POINTS);
			LOG_LPARAM_CASE_1(WM_NCRBUTTONDBLCLK, POINTS);
			LOG_LPARAM_CASE_1(WM_NCRBUTTONDOWN, POINTS);
			LOG_LPARAM_CASE_1(WM_NCRBUTTONUP, POINTS);
			LOG_LPARAM_CASE_1(WM_NCXBUTTONDBLCLK, POINTS);
			LOG_LPARAM_CASE_1(WM_NCXBUTTONDOWN, POINTS);
			LOG_LPARAM_CASE_1(WM_NCXBUTTONUP, POINTS);
			LOG_LPARAM_CASE_1(WM_NEXTMENU, MDINEXTMENU*);
			LOG_LPARAM_CASE_1(WM_NOTIFY, NMHDR*);
			LOG_LPARAM_CASE_1(WM_PARENTNOTIFY, POINTS);
			LOG_LPARAM_CASE_1(WM_POINTERACTIVATE, HWND);
			LOG_LPARAM_CASE_1(WM_POINTERCAPTURECHANGED, HWND);
			LOG_LPARAM_CASE_1(WM_POINTERDOWN, POINTS);
			LOG_LPARAM_CASE_1(WM_POINTERENTER, POINTS);
			LOG_LPARAM_CASE_1(WM_POINTERHWHEEL, POINTS);
			LOG_LPARAM_CASE_1(WM_POINTERLEAVE, POINTS);
			LOG_LPARAM_CASE_1(WM_POINTERUP, POINTS);
			LOG_LPARAM_CASE_1(WM_POINTERUPDATE, POINTS);
			LOG_LPARAM_CASE_1(WM_POINTERWHEEL, POINTS);
			LOG_LPARAM_CASE_1(WM_RBUTTONDBLCLK, POINTS);
			LOG_LPARAM_CASE_1(WM_RBUTTONDOWN, POINTS);
			LOG_LPARAM_CASE_1(WM_RBUTTONUP, POINTS);
			LOG_LPARAM_CASE_1(WM_SETCURSOR, std::pair<SHORT, Hex<WORD>>);
			LOG_LPARAM_CASE_2(WM_SETTEXT, LPCSTR, LPCWSTR);
			LOG_LPARAM_CASE_2(WM_SETTINGCHANGE, LPCSTR, LPCWSTR);
			LOG_LPARAM_CASE_1(WM_SIZE, POINTS);
			LOG_LPARAM_CASE_1(WM_SIZING, RECT*);
			LOG_LPARAM_CASE_1(WM_STYLECHANGED, STYLESTRUCT*);
			LOG_LPARAM_CASE_1(WM_STYLECHANGING, STYLESTRUCT*);
			LOG_LPARAM_CASE_1(WM_SYSCOMMAND, POINTS);
			LOG_LPARAM_CASE_1(WM_GETTITLEBARINFOEX, TITLEBARINFOEX*);
			LOG_LPARAM_CASE_1(WM_TOUCHHITTESTING, TOUCH_HIT_TESTING_INPUT*);
			LOG_LPARAM_CASE_1(WM_VKEYTOITEM, HWND);
			LOG_LPARAM_CASE_1(WM_VSCROLL, HWND);
			LOG_LPARAM_CASE_1(WM_VSCROLLCLIPBOARD, std::pair<WORD, WORD>);
			LOG_LPARAM_CASE_1(WM_WINDOWPOSCHANGED, WINDOWPOS*);
			LOG_LPARAM_CASE_1(WM_WINDOWPOSCHANGING, WINDOWPOS*);
			LOG_LPARAM_CASE_1(WM_XBUTTONDBLCLK, POINTS);
			LOG_LPARAM_CASE_1(WM_XBUTTONDOWN, POINTS);
			LOG_LPARAM_CASE_1(WM_XBUTTONUP, POINTS);

		case WM_NCACTIVATE:
			if (-1 == wm.lParam)
			{
				log << "-1";
			}
			else
			{
				log << reinterpret_cast<HRGN>(wm.lParam);
			}
			break;

		case WM_NCCALCSIZE:
			if (wm.wParam)
			{
				log << reinterpret_cast<NCCALCSIZE_PARAMS*>(wm.lParam);
			}
			else
			{
				log << reinterpret_cast<RECT*>(wm.lParam);
			}
			break;

		default:
			log << hex(wm.lParam);
			break;
		}

#undef LOG_LPARAM_CASE_1
#undef LOG_LPARAM_CASE_2
#undef LOG_PARAM_CASE_1
#undef LOG_PARAM_CASE_2

		return os;
	}
}
