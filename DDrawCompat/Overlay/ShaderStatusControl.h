#pragma once

#include <D3dDdi/MetaShader.h>
#include <Overlay/LabelControl.h>

namespace Overlay
{
	class ShaderStatusControl : public LabelControl
	{
	public:
		ShaderStatusControl(Control& parent, const RECT& rect);

		virtual void draw(HDC dc) override;
		virtual void invalidate() override;

	private:
		void updateStatus();

		D3dDdi::MetaShader::ShaderStatus m_status = D3dDdi::MetaShader::ShaderStatus::Compiling;
		bool m_isShaderStatusInvalidated = true;
	};
}
