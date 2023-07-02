#include <array>
#include <fstream>
#include <sstream>

#include <Common/Log.h>
#include <Config/Settings/AlternatePixelCenter.h>
#include <Config/Settings/Antialiasing.h>
#include <Config/Settings/BltFilter.h>
#include <Config/Settings/ColorKeyMethod.h>
#include <Config/Settings/ConfigHotKey.h>
#include <Config/Settings/DepthFormat.h>
#include <Config/Settings/DisplayFilter.h>
#include <Config/Settings/FontAntialiasing.h>
#include <Config/Settings/FpsLimiter.h>
#include <Config/Settings/RenderColorDepth.h>
#include <Config/Settings/ResolutionScale.h>
#include <Config/Settings/ResolutionScaleFilter.h>
#include <Config/Settings/SpriteDetection.h>
#include <Config/Settings/SpriteFilter.h>
#include <Config/Settings/SpriteTexCoord.h>
#include <Config/Settings/TextureFilter.h>
#include <Config/Settings/VSync.h>
#include <D3dDdi/Device.h>
#include <Gdi/GuiThread.h>
#include <Input/Input.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/SettingControl.h>

namespace
{
	struct SettingRow
	{
		Config::Setting* setting;
		Overlay::SettingControl::UpdateFunc updateFunc;
	};

	const int CAPTION_HEIGHT = 22;
	const int ROW_HEIGHT = 25;
	const int ROWS = 15;

	std::array<SettingRow, 16> g_settingRows = { {
		{ &Config::alternatePixelCenter },
		{ &Config::antialiasing, &D3dDdi::Device::updateAllConfig },
		{ &Config::bltFilter },
		{ &Config::colorKeyMethod, &D3dDdi::Device::updateAllConfig },
		{ &Config::depthFormat, &D3dDdi::Device::updateAllConfig },
		{ &Config::displayFilter },
		{ &Config::fontAntialiasing },
		{ &Config::fpsLimiter },
		{ &Config::renderColorDepth, &D3dDdi::Device::updateAllConfig },
		{ &Config::resolutionScale, &D3dDdi::Device::updateAllConfig },
		{ &Config::resolutionScaleFilter },
		{ &Config::spriteDetection },
		{ &Config::spriteFilter, &D3dDdi::Device::updateAllConfig },
		{ &Config::spriteTexCoord, &D3dDdi::Device::updateAllConfig },
		{ &Config::textureFilter, &D3dDdi::Device::updateAllConfig },
		{ &Config::vSync }
	} };
}

namespace Overlay
{
	ConfigWindow::ConfigWindow()
		: Window(nullptr, { 0, 0, SettingControl::TOTAL_WIDTH + ARROW_SIZE + BORDER / 2, ROWS * ROW_HEIGHT + 80 },
			WS_BORDER, Config::configHotKey.get())
		, m_buttonCount(0)
		, m_focus(nullptr)
	{
		RECT r = { 0, 0, m_rect.right - CAPTION_HEIGHT + 1, CAPTION_HEIGHT };
		m_caption.reset(new LabelControl(*this, r, "DDrawCompat Config Overlay", 0, WS_BORDER | WS_VISIBLE));

		r = { m_rect.right - CAPTION_HEIGHT, 0, m_rect.right, CAPTION_HEIGHT };
		m_captionCloseButton.reset(new ButtonControl(*this, r, "X", onClose));

		r.left = SettingControl::TOTAL_WIDTH;
		r.top = CAPTION_HEIGHT + BORDER;
		r.right = r.left + ARROW_SIZE;
		r.bottom = r.top + ROWS * ROW_HEIGHT;
		m_scrollBar.reset(new ScrollBarControl(*this, r, 0, g_settingRows.size() - ROWS));

		addSettingControls();

		m_closeButton = addButton("Close", onClose);
		m_exportButton = addButton("Export", onExport);
		m_importButton = addButton("Import", onImport);
		m_resetAllButton = addButton("Reset all", onResetAll);

		std::ifstream f(Config::Parser::getOverlayConfigPath());
		std::ostringstream oss;
		oss << f.rdbuf();
		m_fileContent = oss.str();

		updateButtons();
	}

	std::unique_ptr<ButtonControl> ConfigWindow::addButton(const std::string& label, ButtonControl::ClickHandler clickHandler)
	{
		++m_buttonCount;
		RECT r = { 0, 0, 80, 22 };
		OffsetRect(&r, m_rect.right - m_buttonCount * (r.right + BORDER), m_rect.bottom - (r.bottom + BORDER));
		return std::make_unique<ButtonControl>(*this, r, label, clickHandler);
	}

	void ConfigWindow::addSettingControl(Config::Setting& setting, SettingControl::UpdateFunc updateFunc)
	{
		const int index = m_settingControls.size();
		RECT rect = { 0, index * ROW_HEIGHT + BORDER, SettingControl::TOTAL_WIDTH, (index + 1) * ROW_HEIGHT + BORDER };
		OffsetRect(&rect, 0, CAPTION_HEIGHT);
		m_settingControls.emplace_back(*this, rect, setting, updateFunc);
	}

	void ConfigWindow::addSettingControls()
	{
		m_settingControls.clear();
		const int pos = m_scrollBar->getPos();
		for (int i = 0; i < ROWS; ++i)
		{
			auto& row = g_settingRows[pos + i];
			addSettingControl(*row.setting, row.updateFunc);
		}
	}

	RECT ConfigWindow::calculateRect(const RECT& monitorRect) const
	{
		RECT r = { 0, 0, m_rect.right - m_rect.left, m_rect.bottom - m_rect.top };
		OffsetRect(&r, monitorRect.left + (monitorRect.right - monitorRect.left - r.right) / 2,
			monitorRect.top + (monitorRect.bottom - monitorRect.top - r.bottom) / 2);
		return r;
	}

	std::string ConfigWindow::constructFileContent()
	{
		std::ostringstream oss;
		for (auto& settingControl : m_settingControls)
		{
			const auto& setting = settingControl.getSetting();
			const auto value = setting.getValueStr();
			if (value == setting.getBaseValue())
			{
				oss << "# ";
			}
			oss << setting.getName() << " = " << value << std::endl;
		}
		return oss.str();
	}

	void ConfigWindow::exportSettings()
	{
		auto path(Config::Parser::getOverlayConfigPath());
		std::ofstream f(path);
		if (f.fail())
		{
			LOG_ONCE("ERROR: Failed to open overlay config file for writing: " << path.u8string());
			return;
		}

		m_fileContent = constructFileContent();
		f.write(m_fileContent.c_str(), m_fileContent.length());

		for (auto& settingControl : m_settingControls)
		{
			settingControl.getSetting().setExportedValue();
		}

		updateButtons();
	}

	void ConfigWindow::importSettings()
	{
		for (auto& settingControl : m_settingControls)
		{
			settingControl.set(settingControl.getSetting().getExportedValue());
		}
		updateButtons();
	}

	void ConfigWindow::onClose(Control& control)
	{
		static_cast<ConfigWindow*>(control.getParent())->setVisible(false);
	}

	void ConfigWindow::onExport(Control& control)
	{
		static_cast<ConfigWindow*>(control.getParent())->exportSettings();
	}

	void ConfigWindow::onImport(Control& control)
	{
		static_cast<ConfigWindow*>(control.getParent())->importSettings();
	}

	void ConfigWindow::onMouseWheel(POINT pos, SHORT delta)
	{
		m_scrollBar->onMouseWheel(pos, delta);
	}

	void ConfigWindow::onNotify(Control& control)
	{
		if (m_scrollBar.get() == &control)
		{
			addSettingControls();
			onMouseMove(Input::getRelativeCursorPos());
		}
	}

	void ConfigWindow::onResetAll(Control& control)
	{
		static_cast<ConfigWindow*>(control.getParent())->resetSettings();
	}

	void ConfigWindow::resetSettings()
	{
		for (auto& settingControl : m_settingControls)
		{
			settingControl.set(settingControl.getSetting().getBaseValue());
		}
		updateButtons();
	}

	void ConfigWindow::setFocus(SettingControl* control)
	{
		if (m_focus == control)
		{
			return;
		}
		m_focus = control;

		Input::setCapture(m_focus ? static_cast<Control*>(m_focus) : this);

		if (m_focus)
		{
			RECT r = m_focus->getHighlightRect();
			int sf = getScaleFactor();
			r = { r.left * sf, r.top * sf, r.right * sf, r.bottom * sf };
			Gdi::GuiThread::setWindowRgn(m_hwnd, r);
		}
		else
		{
			Gdi::GuiThread::setWindowRgn(m_hwnd, nullptr);
		}

		invalidate();
	}

	void ConfigWindow::setVisible(bool isVisible)
	{
		if (isVisible != Window::isVisible())
		{
			Window::setVisible(isVisible);
			Input::setCapture(isVisible ? this : nullptr);
			setFocus(nullptr);
		}
	}

	void ConfigWindow::updateButtons()
	{
		if (!m_exportButton)
		{
			return;
		}

		bool enableImport = false;
		bool enableReset = false;

		for (auto& settingControl : m_settingControls)
		{
			const auto& setting = settingControl.getSetting();
			const auto value = setting.getValueStr();

			if (value != setting.getBaseValue())
			{
				enableReset = true;
			}

			if (value != setting.getExportedValue())
			{
				enableImport = true;
			}
		}

		m_exportButton->setEnabled(m_fileContent != constructFileContent());
		m_importButton->setEnabled(enableImport);
		m_resetAllButton->setEnabled(enableReset);
	}
}
