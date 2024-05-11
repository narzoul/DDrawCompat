#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/Time.h>
#include <D3dDdi/Adapter.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/Resource.h>
#include <Dll/Dll.h>
#include <DDraw/DirectDrawSurface.h>
#include <DDraw/RealPrimarySurface.h>
#include <Gdi/Cursor.h>
#include <Gdi/GuiThread.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/Steam.h>
#include <Win32/DpiAwareness.h>

namespace
{
	decltype(&DirectDrawCreateEx) g_steamDirectDrawCreateEx = nullptr;
	D3dDdi::Adapter* g_adapter = nullptr;
	IDirectDraw7* g_dd = nullptr;
	IDirect3D7* g_d3d = nullptr;
	IDirectDrawSurface7* g_rt = nullptr;
	D3dDdi::Resource* g_rtResource = nullptr;
	D3dDdi::Resource* g_bbResource = nullptr;
	UINT g_bbSubResourceIndex = 0;
	IDirect3DDevice7* g_dev = nullptr;
	UINT g_width = 0;
	UINT g_height = 0;
	bool g_isOverlayOpen = false;
	long long g_qpcLastRender = 0;
	HWND g_window = nullptr;

	void releaseDevice();
	void* getTargetFunc(void* hookedFunc);

	BOOL WINAPI flushInstructionCache(HANDLE hProcess, LPCVOID lpBaseAddress, SIZE_T dwSize)
	{
		LOG_FUNC("FlushInstructionCache", hProcess, lpBaseAddress, dwSize);
		auto hookedFunc = const_cast<BYTE*>(static_cast<const BYTE*>(lpBaseAddress));
		if ((Dll::g_jmpTargetProcs.DirectDrawCreate == lpBaseAddress ||
			Dll::g_jmpTargetProcs.DirectDrawCreateEx == lpBaseAddress) &&
			0xE9 == hookedFunc[0] && 5 == dwSize)
		{
			int& jmpOffset = *reinterpret_cast<int*>(hookedFunc + 1);
			decltype(&DirectDrawCreateEx) directDrawCreateEx = nullptr;
			if (!g_steamDirectDrawCreateEx && Dll::g_jmpTargetProcs.DirectDrawCreateEx == lpBaseAddress)
			{
				directDrawCreateEx = reinterpret_cast<decltype(&DirectDrawCreateEx)>(hookedFunc + 5 + jmpOffset);
			}

			auto targetFunc = getTargetFunc(hookedFunc);
			auto gameOverlayRenderer = GetModuleHandle("GameOverlayRenderer.dll");
			if (gameOverlayRenderer && Compat::getModuleHandleFromAddress(targetFunc) == gameOverlayRenderer)
			{
				if (directDrawCreateEx)
				{
					g_steamDirectDrawCreateEx = directDrawCreateEx;
					LOG_INFO << "Steam overlay support activated";
				}

				LOG_DEBUG << "Restoring hook: " << Compat::funcPtrToStr(lpBaseAddress);
				DWORD oldProtect = 0;
				VirtualProtect(hookedFunc, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
				auto newFunc = Dll::g_jmpTargetProcs.DirectDrawCreate == lpBaseAddress
					? Dll::g_newProcs.DirectDrawCreate : Dll::g_newProcs.DirectDrawCreateEx;
				jmpOffset = reinterpret_cast<BYTE*>(newFunc) - (hookedFunc + 5);
				VirtualProtect(hookedFunc, 5, PAGE_EXECUTE_READ, &oldProtect);
			}
		}
		return LOG_RESULT(CALL_ORIG_FUNC(FlushInstructionCache)(hProcess, lpBaseAddress, dwSize));
	}

	HWND WINAPI getActiveWindow()
	{
		LOG_FUNC("steamGetActiveWindow");
		return LOG_RESULT(nullptr);
	}

	BOOL WINAPI getClientRect(HWND hWnd, LPRECT lpRect)
	{
		LOG_FUNC("steamGetClientRect", hWnd, lpRect);
		Win32::ScopedDpiAwareness dpiAwareness;
		return LOG_RESULT(GetClientRect(hWnd, lpRect));
	}

	HWND WINAPI getForegroundWindow()
	{
		LOG_FUNC("steamGetForegroundWindow");
		HWND result = GetForegroundWindow();
		if (result)
		{
			HWND presentationWindow = DDraw::RealPrimarySurface::getPresentationWindow();
			if (presentationWindow && GetParent(presentationWindow) == result)
			{
				g_window = presentationWindow;
				return LOG_RESULT(presentationWindow);
			}
		}
		return LOG_RESULT(nullptr);
	}

	void* getTargetFunc(void* hookedFunc)
	{
		auto targetFunc = reinterpret_cast<BYTE*>(hookedFunc);
		while (0xE9 == targetFunc[0])
		{
			targetFunc += 5 + *reinterpret_cast<int*>(targetFunc + 1);
		}
		return targetFunc;
	}

	template <typename Intf>
	void release(Intf*& intf)
	{
		if (intf)
		{
			intf->lpVtbl->Release(intf);
			intf = nullptr;
		}
	}

	void releaseAll()
	{
		releaseDevice();
		release(g_d3d);
		release(g_dd);
		g_adapter = nullptr;
	}

	void releaseDevice()
	{
		release(g_dev);
		release(g_rt);
		g_rtResource = nullptr;
		g_width = 0;
		g_height = 0;
	}

	void* removeHook(HMODULE gameOverlayRenderer, const wchar_t* origDDrawModulePath, void* hookedFunc)
	{
		LOG_FUNC("Steam::removeHook", gameOverlayRenderer, origDDrawModulePath, hookedFunc);
		auto targetFunc = getTargetFunc(hookedFunc);
		if (targetFunc != hookedFunc && Compat::getModuleHandleFromAddress(targetFunc) == gameOverlayRenderer)
		{
			auto offset = Compat::getModuleFileOffset(hookedFunc);
			if (0 == offset)
			{
				return LOG_RESULT(nullptr);
			}

			std::ifstream f(origDDrawModulePath, std::ios::in | std::ios::binary);
			f.seekg(offset);
			char instructions[20] = {};
			f.read(instructions, sizeof(instructions));
			f.close();

			unsigned totalInstructionSize = 0;
			while (totalInstructionSize < 5)
			{
				unsigned instructionSize = Compat::getInstructionSize(instructions + totalInstructionSize);
				if (0 == instructionSize)
				{
					return LOG_RESULT(nullptr);
				}
				totalInstructionSize += instructionSize;
			}

			DWORD oldProtect = 0;
			VirtualProtect(hookedFunc, totalInstructionSize, PAGE_EXECUTE_READWRITE, &oldProtect);
			memcpy(hookedFunc, instructions, totalInstructionSize);
			VirtualProtect(hookedFunc, totalInstructionSize, PAGE_EXECUTE_READ, &oldProtect);
			CALL_ORIG_FUNC(FlushInstructionCache)(GetCurrentProcess(), hookedFunc, totalInstructionSize);

			return LOG_RESULT(targetFunc);
		}
		return LOG_RESULT(nullptr);
	}

	LONG WINAPI setClassLongW(HWND hWnd, int nIndex, LONG dwNewLong)
	{
		LOG_FUNC("steamSetClassLongW", hWnd, nIndex, dwNewLong);
		if (GCLP_HCURSOR == nIndex)
		{
			HWND presentationWindow = DDraw::RealPrimarySurface::getPresentationWindow();
			if (presentationWindow && presentationWindow == hWnd)
			{
				g_isOverlayOpen = 0 == dwNewLong;
				LOG_DEBUG << "Steam overlay is " << (g_isOverlayOpen ? "opening" : "closing");

				auto exStyle = CALL_ORIG_FUNC(GetWindowLongA)(hWnd, GWL_EXSTYLE);
				CALL_ORIG_FUNC(SetWindowLongA)(hWnd, GWL_EXSTYLE,
					g_isOverlayOpen ? (exStyle & ~WS_EX_TRANSPARENT) : (exStyle | WS_EX_TRANSPARENT));

				if (g_isOverlayOpen)
				{
					auto configWindow = Gdi::GuiThread::getConfigWindow();
					if (configWindow)
					{
						configWindow->setVisible(false);
					}
					Gdi::Cursor::setEmulated(false);
				}
			}
		}
		return LOG_RESULT(CALL_ORIG_FUNC(SetWindowLongW)(hWnd, nIndex, dwNewLong));
	}

	bool updateDevice(D3dDdi::Resource& resource)
	{
		auto adapter = &resource.getDevice().getAdapter();
		if (adapter != g_adapter)
		{
			releaseAll();
			g_adapter = adapter;
		}

		if (!g_dd)
		{
			auto result = g_steamDirectDrawCreateEx(
				adapter->getGuid(), reinterpret_cast<void**>(&g_dd), IID_IDirectDraw7, nullptr);
			if (FAILED(result))
			{
				LOG_ONCE("Failed to create DirectDraw object for Steam overlay: " << Compat::hex(result));
				return false;
			}

			result = g_dd->lpVtbl->SetCooperativeLevel(g_dd, nullptr, DDSCL_NORMAL | DDSCL_FPUPRESERVE);
			if (FAILED(result))
			{
				LOG_ONCE("Failed to set cooperative level for Steam overlay: " << Compat::hex(result));
				release(g_dd);
				return false;
			}
		}

		if (!g_d3d)
		{
			auto result = g_dd->lpVtbl->QueryInterface(g_dd, IID_IDirect3D7, reinterpret_cast<void**>(&g_d3d));
			if (FAILED(result))
			{
				LOG_ONCE("Failed to create Direct3D object for Steam overlay: " << Compat::hex(result));
				return false;
			}
		}

		auto& si = resource.getFixedDesc().pSurfList[0];
		if (!g_rt || si.Width != g_width || si.Height != g_height || FAILED(g_rt->lpVtbl->IsLost(g_rt)))
		{
			releaseDevice();

			DDSURFACEDESC2 desc = {};
			desc.dwSize = sizeof(desc);
			desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS;
			desc.dwWidth = si.Width;
			desc.dwHeight = si.Height;
			desc.ddpfPixelFormat = D3dDdi::getPixelFormat(D3DDDIFMT_X8R8G8B8);
			desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY;

			auto& formats = adapter->getInfo().formatOps;
			if (formats.find(D3dDdi::FOURCC_NULL) != formats.end())
			{
				D3dDdi::Resource::setFormatOverride(D3dDdi::FOURCC_NULL);
			}
			D3dDdi::Resource::enableConfig(false);
			auto result = g_dd->lpVtbl->CreateSurface(g_dd, &desc, &g_rt, nullptr);
			D3dDdi::Resource::enableConfig(true);
			D3dDdi::Resource::setFormatOverride(D3DDDIFMT_UNKNOWN);

			if (FAILED(result))
			{
				LOG_ONCE("Failed to create DirectDrawSurface object for Steam overlay: " << Compat::hex(result));
				return false;
			}

			g_width = si.Width;
			g_height = si.Height;
			g_rtResource = D3dDdi::Device::findResource(DDraw::DirectDrawSurface::getDriverResourceHandle(*g_rt));
		}

		if (!g_dev)
		{
			auto result = g_d3d->lpVtbl->CreateDevice(g_d3d, IID_IDirect3DHALDevice, g_rt, &g_dev);
			if (FAILED(result))
			{
				LOG_ONCE("Failed to create Direct3DDevice for Steam overlay: " << Compat::hex(result));
				return false;
			}

			g_dev->lpVtbl->BeginScene(g_dev);
			g_dev->lpVtbl->Clear(g_dev, 0, nullptr, D3DCLEAR_TARGET, 0, 0, 0);
			g_dev->lpVtbl->EndScene(g_dev);
		}

		return true;
	}
}

namespace Overlay
{
	namespace Steam
	{
		void flush()
		{
			if (!g_steamDirectDrawCreateEx)
			{
				return;
			}

			auto qpcNow = Time::queryPerformanceCounter();
			if (g_isOverlayOpen || qpcNow - g_qpcLastRender > Time::g_qpcFrequency / 20)
			{
				DDraw::RealPrimarySurface::scheduleOverlayUpdate();
			}
		}

		Resources getResources()
		{
			Resources resources = {};
			resources.rtResource = g_rtResource;
			resources.bbResource = g_bbResource;
			resources.bbSubResourceIndex = g_bbSubResourceIndex;
			return resources;
		}

		HWND getWindow()
		{
			return g_window;
		}

		void init(const wchar_t* origDDrawModulePath)
		{
			HOOK_FUNCTION(kernel32, FlushInstructionCache, flushInstructionCache);

			auto gameOverlayRenderer = GetModuleHandle("GameOverlayRenderer.dll");
			if (!gameOverlayRenderer)
			{
				return;
			}

			removeHook(gameOverlayRenderer, origDDrawModulePath, Dll::g_jmpTargetProcs.DirectDrawCreate);
			g_steamDirectDrawCreateEx = static_cast<decltype(&DirectDrawCreateEx)>(
				removeHook(gameOverlayRenderer, origDDrawModulePath, Dll::g_jmpTargetProcs.DirectDrawCreateEx));

			if (g_steamDirectDrawCreateEx)
			{
				LOG_INFO << "Steam overlay support activated";
			}
		}

		void installHooks()
		{
			auto gameOverlayRenderer = GetModuleHandle("GameOverlayRenderer.dll");
			if (!gameOverlayRenderer)
			{
				return;
			}

			LOG_INFO << "Installing Steam overlay hooks";
			Compat::hookIatFunction(gameOverlayRenderer, "GetActiveWindow", getActiveWindow);
			Compat::hookIatFunction(gameOverlayRenderer, "GetClientRect", getClientRect);
			Compat::hookIatFunction(gameOverlayRenderer, "GetForegroundWindow", getForegroundWindow);
			Compat::hookIatFunction(gameOverlayRenderer, "SetClassLongW", setClassLongW);
		}

		bool isOverlayOpen()
		{
			return g_isOverlayOpen;
		}

		void onDestroyWindow(HWND hwnd)
		{
			if (hwnd == g_window)
			{
				g_window = nullptr;
			}
		}

		void render(D3dDdi::Resource& resource, unsigned subResourceIndex)
		{
			LOG_FUNC("Steam::render", resource, subResourceIndex);
			if (!g_steamDirectDrawCreateEx || !updateDevice(resource))
			{
				return;
			}

			g_bbResource = &resource;
			g_bbSubResourceIndex = subResourceIndex;
			g_dev->lpVtbl->BeginScene(g_dev);
			g_dev->lpVtbl->EndScene(g_dev);
			g_bbResource = nullptr;
			g_bbSubResourceIndex = 0;
			g_qpcLastRender = Time::queryPerformanceCounter();
		}
	}
}
