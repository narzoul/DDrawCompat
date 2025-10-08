#pragma once

#include <string>

#include <Windows.h>

#include <Overlay/LabelControl.h>

namespace Overlay
{
	class ButtonControl : public Control
	{
	public:
		typedef void (*ClickHandler)(ButtonControl&);

		ButtonControl(Control& parent, const RECT& rect, const std::string& label, ClickHandler clickHandler);

		virtual void onLButtonDown(POINT pos) override;
		virtual void onLButtonUp(POINT pos) override;
		virtual void onMouseMove(POINT pos) override;

	private:
		ClickHandler m_clickHandler;
		LabelControl m_label;
	};
}
