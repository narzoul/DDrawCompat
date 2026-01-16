#include <D3dDdi/Adapter.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/MetaShader.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <Gdi/GuiThread.h>
#include <Overlay/ComboBoxControl.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/ShaderStatusControl.h>

namespace
{
	D3dDdi::MetaShader::ShaderStatus getShaderStatus()
	{
		D3dDdi::ScopedCriticalSection lock;
		for (auto& device : D3dDdi::Device::getDevices())
		{
			MONITORINFOEXW mi = {};
			mi.cbSize = sizeof(mi);
			CALL_ORIG_FUNC(GetMonitorInfoW)(
				CALL_ORIG_FUNC(MonitorFromWindow)(Gdi::GuiThread::getConfigWindow()->getWindow(), MONITOR_DEFAULTTOPRIMARY), &mi);
			if (device.second.getAdapter().getDeviceName() == mi.szDevice)
			{
				return device.second.getShaderBlitter().getMetaShader().getStatus();
			}
		}
		return D3dDdi::MetaShader::ShaderStatus::Compiling;
	}
}

namespace Overlay
{
	ShaderStatusControl::ShaderStatusControl(Control& parent, const RECT& rect)
		: LabelControl(parent, rect, "Compiling...", TA_LEFT)
	{
		setColor(HIGHLIGHT_COLOR);
	}

	void ShaderStatusControl::draw(HDC dc)
	{
		updateStatus();
		LabelControl::draw(dc);
	}

	void ShaderStatusControl::invalidate()
	{
		m_isShaderStatusInvalidated = true;
		LabelControl::invalidate();
	}

	void ShaderStatusControl::updateStatus()
	{
		if (!m_isShaderStatusInvalidated)
		{
			return;
		}
		m_isShaderStatusInvalidated = false;

		auto status = getShaderStatus();
		if (status == m_status)
		{
			return;
		}
		m_status = status;

		switch (m_status)
		{
		case D3dDdi::MetaShader::ShaderStatus::Compiling:
			break;
		case D3dDdi::MetaShader::ShaderStatus::Compiled:
			setLabel("Compiled");
			setColor(FOREGROUND_COLOR);
			break;
		default:
			switch (m_status)
			{
			case D3dDdi::MetaShader::ShaderStatus::ParseError:
				setLabel("Parse error");
				break;
			case D3dDdi::MetaShader::ShaderStatus::CompileError:
				setLabel("Compile error");
				break;
			case D3dDdi::MetaShader::ShaderStatus::SetupError:
				setLabel("Setup error");
				break;
			case D3dDdi::MetaShader::ShaderStatus::TextureError:
				setLabel("Image error");
				break;
			case D3dDdi::MetaShader::ShaderStatus::RenderError:
				setLabel("Render error");
				break;
			case D3dDdi::MetaShader::ShaderStatus::ResolutionError:
				setLabel("Res too high");
				break;
			case D3dDdi::MetaShader::ShaderStatus::SurfaceError:
				setLabel("Surface error");
				break;
			}
			setColor(ERROR_COLOR);
			break;
		}
	}
}
