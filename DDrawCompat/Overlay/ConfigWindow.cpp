#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

#include <Common/Log.h>
#include <Config/Settings/AlternatePixelCenter.h>
#include <Config/Settings/Antialiasing.h>
#include <Config/Settings/BltFilter.h>
#include <Config/Settings/ColorKeyMethod.h>
#include <Config/Settings/ConfigHotKey.h>
#include <Config/Settings/ConfigRows.h>
#include <Config/Settings/ConfigTransparency.h>
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
#include <Config/Settings/StatsPosX.h>
#include <Config/Settings/StatsPosY.h>
#include <Config/Settings/StatsTransparency.h>
#include <Config/Settings/TextureFilter.h>
#include <Config/Settings/VertexFixup.h>
#include <Config/Settings/VSync.h>
#include <D3dDdi/Device.h>
#include <Gdi/GuiThread.h>
#include <Input/Input.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/SettingControl.h>
#include <Overlay/StatsWindow.h>
#include <Overlay/Steam.h>

namespace
{
	struct SettingRow
	{
		Config::Setting* setting;
		Overlay::SettingControl::UpdateFunc updateFunc;
	};

	const int CAPTION_HEIGHT = 22;
	const int ROW_HEIGHT = 25;
	const int ROWS = 16;

	std::vector<SettingRow> g_settingRows = {
		{ &Config::alternatePixelCenter },
		{ &Config::antialiasing, &D3dDdi::Device::updateAllConfig },
		{ &Config::bltFilter },
		{ &Config::colorKeyMethod, &D3dDdi::Device::updateAllConfig },
		{ &Config::configTransparency, [&]() { Gdi::GuiThread::getConfigWindow()->setAlpha(Config::configTransparency.get()); }},
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
		{ &Config::statsPosX, []() { Gdi::GuiThread::getStatsWindow()->updatePos(); } },
		{ &Config::statsPosY, []() { Gdi::GuiThread::getStatsWindow()->updatePos(); } },
		{ &Config::statsTransparency, [&]() { Gdi::GuiThread::getStatsWindow()->setAlpha(Config::statsTransparency.get()); }},
		{ &Config::textureFilter, &D3dDdi::Device::updateAllConfig },
		{ &Config::vertexFixup, &D3dDdi::Device::updateAllConfig },
		{ &Config::vSync }
	};
}

namespace Overlay
{
	ConfigWindow::ConfigWindow()
		: Window(nullptr, { 0, 0, VIRTUAL_SCREEN_WIDTH, VIRTUAL_SCREEN_HEIGHT },
			WS_BORDER, Config::configTransparency.get(), Config::configHotKey.get())
		, m_buttonCount(0)
		, m_focus(nullptr)
	{
		RECT r = { 0, 0, m_rect.right - CAPTION_HEIGHT + 1, CAPTION_HEIGHT };
		m_caption.reset(new LabelControl(*this, r, "DDrawCompat Config Overlay", 0, WS_BORDER | WS_VISIBLE));

		r = { m_rect.right - CAPTION_HEIGHT, 0, m_rect.right, CAPTION_HEIGHT };
		m_captionCloseButton.reset(new ButtonControl(*this, r, "X", onClose));

		r.right = m_rect.right - BORDER;
		r.left = r.right - ARROW_SIZE;
		r.top = CAPTION_HEIGHT + BORDER;
		r.bottom = r.top + ROWS * ROW_HEIGHT;

		const auto settingCount = Config::configRows.get().size();
		m_scrollBar.reset(new ScrollBarControl(*this, r, 0, settingCount - ROWS, 0,
			WS_VISIBLE | (settingCount < ROWS ? WS_DISABLED : 0)));

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

	void ConfigWindow::addSettingControl(Config::Setting& setting, SettingControl::UpdateFunc updateFunc, bool isReadOnly)
	{
		const int index = m_settingControls.size();
		RECT rect = { 0, index * ROW_HEIGHT + BORDER, SettingControl::TOTAL_WIDTH, (index + 1) * ROW_HEIGHT + BORDER };
		OffsetRect(&rect, 0, CAPTION_HEIGHT);
		m_settingControls.emplace_back(*this, rect, setting, updateFunc, isReadOnly);
	}

	void ConfigWindow::addSettingControls()
	{
		m_settingControls.clear();
		const auto& configRows = Config::configRows.get();
		const unsigned pos = m_scrollBar->getPos();

		for (int i = 0; i < ROWS && pos + i < configRows.size(); ++i)
		{
			const auto setting = configRows[pos + i];
			const auto it = std::find_if(g_settingRows.begin(), g_settingRows.end(),
				[&](auto& settingRow) { return setting == settingRow.setting; });
			const bool isReadOnly = it == g_settingRows.end();
			addSettingControl(*setting, isReadOnly ? SettingControl::UpdateFunc() : it->updateFunc, isReadOnly);
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

	std::set<std::string> ConfigWindow::getRwSettingNames()
	{
		std::set<std::string> names;
		for (const auto& row : g_settingRows)
		{
			names.insert(row.setting->getName());
		}
		return names;
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
			if (isVisible && Overlay::Steam::isOverlayOpen())
			{
				return;
			}

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
