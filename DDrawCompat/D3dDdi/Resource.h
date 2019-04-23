#pragma once

#include <vector>

#include <d3d.h>
#include <d3dumddi.h>

namespace D3dDdi
{
	class Device;

	class Resource
	{
	public:
		static Resource create(Device& device, D3DDDIARG_CREATERESOURCE& data);
		static Resource create(Device& device, D3DDDIARG_CREATERESOURCE2& data);

		operator HANDLE() const { return m_handle; }

		HRESULT blt(D3DDDIARG_BLT data);
		HRESULT lock(D3DDDIARG_LOCK& data);
		HRESULT unlock(const D3DDDIARG_UNLOCK& data);

	private:
		class Data : public D3DDDIARG_CREATERESOURCE2
		{
		public:
			Data();
			Data(const D3DDDIARG_CREATERESOURCE& data);
			Data(const D3DDDIARG_CREATERESOURCE2& data);
			Data(const Data& other);
			Data& operator=(const Data& other);
			Data(Data&&) = default;
			Data& operator=(Data&&) = default;

			std::vector<D3DDDI_SURFACEINFO> surfaceData;
		};

		Resource(Device& device, const D3DDDIARG_CREATERESOURCE& data);
		Resource(Device& device, const D3DDDIARG_CREATERESOURCE2& data);

		template <typename Arg>
		static Resource create(Device& device, Arg& data, HRESULT(APIENTRY *createResourceFunc)(HANDLE, Arg*));

		bool isOversized() const;
		HRESULT splitBlt(D3DDDIARG_BLT& data, UINT& subResourceIndex, RECT& rect, RECT& otherRect);

		template <typename Arg>
		HRESULT splitLock(Arg& data, HRESULT(APIENTRY *lockFunc)(HANDLE, Arg*));

		Device& m_device;
		HANDLE m_handle;
		Data m_origData;
		Data m_fixedData;
	};
}
