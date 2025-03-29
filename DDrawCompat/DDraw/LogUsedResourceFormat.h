#include <ddraw.h>

namespace DDraw
{
	class LogUsedResourceFormat
	{
	public:
		LogUsedResourceFormat(const DDSURFACEDESC2& desc, IUnknown*& surface, HRESULT& result);
		~LogUsedResourceFormat();

	private:
		DDSURFACEDESC2 m_desc;
		IUnknown*& m_surface;
		HRESULT& m_result;
	};

	class SuppressResourceFormatLogs
	{
	public:
		SuppressResourceFormatLogs();
		~SuppressResourceFormatLogs();
	};
}
