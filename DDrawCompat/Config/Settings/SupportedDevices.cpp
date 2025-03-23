#include <d3d.h>

#include <Common/Comparison.h>
#include <Config/Parser.h>
#include <Config/Settings/SupportedDevices.h>

namespace Config
{
	namespace Settings
	{
		SupportedDevices::SupportedDevices()
			: ListSetting("SupportedDevices", "all")
			, m_allowAll(false)
		{
		}

		std::string SupportedDevices::addValue(const std::string& value)
		{
			if ("all" == value)
			{
				m_devices.clear();
				m_allowAll = true;
				return value;
			}

			if ("ramp" == value)
			{
				m_devices.insert(IID_IDirect3DRampDevice);
			}
			else if ("rgb" == value)
			{
				m_devices.insert(IID_IDirect3DRGBDevice);
			}
			else if ("hal" == value)
			{
				m_devices.insert(IID_IDirect3DHALDevice);
			}
			else if ("mmx" == value)
			{
				m_devices.insert(IID_IDirect3DMMXDevice);
			}
			else if ("ref" == value)
			{
				m_devices.insert(IID_IDirect3DRefDevice);
			}
			else if ("tnl" == value)
			{
				m_devices.insert(IID_IDirect3DTnLHalDevice);
			}
			else
			{
				throw ParsingError("invalid value: '" + value + "'");
			}

			return value;
		}

		void SupportedDevices::clear()
		{
			m_devices.clear();
			m_allowAll = false;
		}

		bool SupportedDevices::isSupported(const IID& deviceType) const
		{
			if (m_allowAll)
			{
				return true;
			}
			return m_devices.find(deviceType) != m_devices.end();
		}
	}
}