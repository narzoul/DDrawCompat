#pragma once

namespace D3dDdi
{
	class Resource;
}

namespace Overlay
{
	namespace Steam
	{
		struct Resources
		{
			D3dDdi::Resource* rtResource;
			D3dDdi::Resource* bbResource;
			UINT bbSubResourceIndex;
		};

		void flush();
		Resources getResources();
		HWND getWindow();
		void init(const wchar_t* origDDrawModulePath);
		void installHooks();
		bool isOverlayOpen();
		void onDestroyWindow(HWND hwnd);
		void render(D3dDdi::Resource& resource, unsigned subResourceIndex);
	}
}
