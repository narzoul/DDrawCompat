#pragma once

#include <Windows.h>

namespace D3dDdi
{
	class ResourceDeleter
	{
	public:
		typedef HRESULT(APIENTRY* Deleter)(HANDLE, HANDLE);

		ResourceDeleter(HANDLE device = nullptr, Deleter deleter = nullptr)
			: m_device(device)
			, m_deleter(deleter)
		{
		}

		void operator()(HANDLE resource)
		{
			m_deleter(m_device, resource);
		}

	private:
		HANDLE m_device;
		Deleter m_deleter;
	};
}
