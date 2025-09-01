#include <D3dDdi/Device.h>
#include <D3dDdi/MetaShader.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <Overlay/ComboBoxControl.h>
#include <Overlay/SettingControl.h>
#include <Overlay/ShaderStatusControl.h>

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

	void ShaderStatusControl::updateStatus()
	{
		if (static_cast<int>(m_status) > static_cast<int>(D3dDdi::MetaShader::ShaderStatus::ParseError))
		{
			return;
		}

		auto valueControl = static_cast<Overlay::SettingControl&>(*m_parent).getValueControl();
		auto relPath = static_cast<ComboBoxControl*>(valueControl)->getValue();

		{
			D3dDdi::ScopedCriticalSection lock;
			for (auto& device : D3dDdi::Device::getDevices())
			{
				if (relPath == device.second.getShaderBlitter().getMetaShader().getRelPath().u8string())
				{
					const auto status = device.second.getShaderBlitter().getMetaShader().getStatus();
					if (status == m_status)
					{
						return;
					}
					m_status = status;
					break;
				}
			}
		}

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
