#pragma once

#include <memory>
#include <vector>

#include <ddraw.h>

#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>

namespace DDraw
{
	template <typename TSurface> class SurfaceImpl;
	template <typename TSurface> class SurfaceImpl2;

	class Surface
	{
	public:
		static const DWORD ALIGNMENT = 32;

		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, LPVOID*);
		virtual ULONG STDMETHODCALLTYPE AddRef();
		virtual ULONG STDMETHODCALLTYPE Release();

		Surface(DWORD origCaps);
		virtual ~Surface();

		static void* alignBuffer(void* buffer);

		template <typename TDirectDraw, typename TSurface, typename TSurfaceDesc>
		static HRESULT create(
			CompatRef<TDirectDraw> dd, TSurfaceDesc desc, TSurface*& surface, std::unique_ptr<Surface> privateData);

		template <typename TSurface>
		static Surface* getSurface(TSurface& dds);

		CompatWeakPtr<IDirectDrawSurface7> getDDS() const { return m_surface; };

		template <typename TSurface>
		SurfaceImpl<TSurface>* getImpl() const;

		virtual void restore();

		void setSizeOverride(DWORD width, DWORD height);

	protected:
		static void attach(CompatRef<IDirectDrawSurface7> dds, std::unique_ptr<Surface> privateData);

		virtual void createImpl();

		void fixAlignment(CompatRef<IDirectDrawSurface7> surface);

		std::unique_ptr<SurfaceImpl<IDirectDrawSurface>> m_impl;
		std::unique_ptr<SurfaceImpl<IDirectDrawSurface2>> m_impl2;
		std::unique_ptr<SurfaceImpl<IDirectDrawSurface3>> m_impl3;
		std::unique_ptr<SurfaceImpl<IDirectDrawSurface4>> m_impl4;
		std::unique_ptr<SurfaceImpl<IDirectDrawSurface7>> m_impl7;

		CompatWeakPtr<IDirectDrawSurface7> m_surface;

	private:
		template <typename TDirectDrawSurface>
		friend class SurfaceImpl;

		DWORD m_origCaps;
		DWORD m_refCount;
		SIZE m_sizeOverride;
		std::unique_ptr<void, void(*)(void*)> m_sysMemBuffer;
	};
}
