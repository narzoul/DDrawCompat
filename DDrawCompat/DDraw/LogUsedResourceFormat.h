#include <ddraw.h>

namespace DDraw
{
	class LogUsedResourceFormat
	{
	public:
		LogUsedResourceFormat(const DDSURFACEDESC2& desc, IUnknown*& surface);
		~LogUsedResourceFormat();

	private:
		DDSURFACEDESC2 m_desc;
		IUnknown*& m_surface;
	};

	class SuppressResourceFormatLogs
	{
	public:
		SuppressResourceFormatLogs();
		~SuppressResourceFormatLogs();
	};
}
