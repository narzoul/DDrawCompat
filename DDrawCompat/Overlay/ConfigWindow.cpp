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
#include <Config/Settings/DisplayAspectRatio.h>
#include <Config/Settings/DisplayFilter.h>
#include <Config/Settings/FontAntialiasing.h>
#include <Config/Settings/FpsLimiter.h>
#include <Config/Settings/MousePollingRate.h>
#include <Config/Settings/MouseSensitivity.h>
#include <Config/Settings/PresentDelay.h>
#include <Config/Settings/RenderColorDepth.h>
#include <Config/Settings/ResolutionScale.h>
#include <Config/Settings/ResolutionScaleFilter.h>
#include <Config/Settings/SpriteAltPixelCenter.h>
#include <Config/Settings/SpriteDetection.h>
#include <Config/Settings/SpriteFilter.h>
#include <Config/Settings/SpriteTexCoord.h>
#include <Config/Settings/StatsPosX.h>
#include <Config/Settings/StatsPosY.h>
#include <Config/Settings/StatsTransparency.h>
#include <Config/Settings/TextureFilter.h>
#include <Config/Settings/VertexFixup.h>
#include <Config/Settings/ViewportEdgeFix.h>
#include <Config/Settings/VSync.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <DDraw/RealPrimarySurface.h>
#include <Gdi/Gdi.h>
#include <Gdi/GuiThread.h>
#include <Input/Input.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/SettingControl.h>
#include <Overlay/ShaderSettingControl.h>
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
		{ &Config::configTransparency, []() { Gdi::GuiThread::getConfigWindow()->setAlpha(Config::configTransparency.get()); }},
		{ &Config::depthFormat, &D3dDdi::Device::updateAllConfig },
		{ &Config::displayAspectRatio, &D3dDdi::Device::updateAllConfig },
		{ &Config::displayFilter, []() { Gdi::GuiThread::getConfigWindow()->updateDisplayFilter(); }},
		{ &Config::fontAntialiasing },
		{ &Config::fpsLimiter, &DDraw::RealPrimarySurface::updateFpsLimiter },
		{ &Config::mousePollingRate, &Input::updateMouseSensitivity },
		{ &Config::mouseSensitivity, &Input::updateMouseSensitivitySetting },
		{ &Config::presentDelay },
		{ &Config::renderColorDepth, &D3dDdi::Device::updateAllConfig },
		{ &Config::resolutionScale, &D3dDdi::Device::updateAllConfig },
		{ &Config::resolutionScaleFilter },
		{ &Config::spriteAltPixelCenter },
		{ &Config::spriteDetection },
		{ &Config::spriteFilter, &D3dDdi::Device::updateAllConfig },
		{ &Config::spriteTexCoord, &D3dDdi::Device::updateAllConfig },
		{ &Config::statsPosX, []() { if (auto statsWindow = Gdi::GuiThread::getStatsWindow()) { statsWindow->updatePos(); } } },
		{ &Config::statsPosY, []() { if (auto statsWindow = Gdi::GuiThread::getStatsWindow()) { statsWindow->updatePos(); } } },
		{ &Config::statsTransparency, []() {
			if (auto statsWindow = Gdi::GuiThread::getStatsWindow()) { statsWindow->setAlpha(Config::statsTransparency.get()); } } },
		{ &Config::textureFilter, &D3dDdi::Device::updateAllConfig },
		{ &Config::vertexFixup, &D3dDdi::Device::updateAllConfig },
		{ &Config::viewportEdgeFix },
		{ &Config::vSync }
	};
}

namespace Overlay
{
	ConfigWindow::ConfigWindow()
		: Window(nullptr, { 0, 0, VIRTUAL_SCREEN_WIDTH, VIRTUAL_SCREEN_HEIGHT },
			WS_BORDER, Config::configTransparency.get(), Config::configHotKey.get())
		, m_buttonCount(0)
		, m_displayFilterSettingControl(nullptr)
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
		m_scrollBar.reset(new ScrollBarControl(*this, r, 0, settingCount - 1, 0,
			WS_VISIBLE | (settingCount <= 1 ? WS_DISABLED : 0)));

		updateDisplayFilter();
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

	void ConfigWindow::addSettingControls()
	{
		m_displayFilterSettingControl = nullptr;
		m_settingControls.clear();
		const auto& configRows = Config::configRows.get();
		const unsigned pos = m_scrollBar->getPos();

		int displayFilterIndex = INT_MAX;
		for (unsigned i = 0; i < configRows.size(); ++i)
		{
			if (&Config::displayFilter == configRows[i])
			{
				displayFilterIndex = i;
				break;
			}
		}

		for (int i = 0; i < ROWS && pos + i < configRows.size() + m_shaderParameters.size(); ++i)
		{
			int row = pos + i;
			if (row > displayFilterIndex)
			{
				const std::size_t shaderParamIndex = row - displayFilterIndex - 1;
				if (shaderParamIndex < m_shaderParameters.size())
				{
					m_settingControls.push_back(std::make_unique<ShaderSettingControl>(
						*this, getNextSettingControlRect(), m_shaderParameters[shaderParamIndex]));
					continue;
				}
				row -= m_shaderParameters.size();
			}

			const auto setting = configRows[row];
			const auto it = std::find_if(g_settingRows.begin(), g_settingRows.end(),
				[&](auto& settingRow) { return setting == settingRow.setting; });
			const bool isReadOnly = it == g_settingRows.end();
			m_settingControls.push_back(std::make_unique<SettingControl>(*this, getNextSettingControlRect(),
				*setting, isReadOnly ? SettingControl::UpdateFunc() : it->updateFunc, isReadOnly));
			if (&Config::displayFilter == setting)
			{
				m_displayFilterSettingControl = static_cast<SettingControl*>(m_settingControls.back().get());
			}
		}
	}

	RECT ConfigWindow::calculateRect(const RECT& monitorRect) const
	{
		RECT r = { 0, 0, m_rect.right - m_rect.left, m_rect.bottom - m_rect.top };
		OffsetRect(&r, monitorRect.left + (monitorRect.right - monitorRect.left - r.right) / 2,
			monitorRect.top + (monitorRect.bottom - monitorRect.top - r.bottom) / 2);
		invalidateShaderStatus();
		return r;
	}

	std::string ConfigWindow::constructFileContent()
	{
		std::ostringstream oss;
		for (auto& settingRow : g_settingRows)
		{
			const auto& setting = *settingRow.setting;
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

		for (auto& settingRow : g_settingRows)
		{
			settingRow.setting->setExportedValue();
		}

		updateButtons();
	}

	RECT ConfigWindow::getNextSettingControlRect() const
	{
		const int index = m_settingControls.size();
		RECT rect = { 0, 0, SettingControl::TOTAL_WIDTH, ROW_HEIGHT };
		OffsetRect(&rect, 0, CAPTION_HEIGHT + BORDER + index * ROW_HEIGHT);
		return rect;
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
		updateSettings([](const Config::Setting& setting) { return setting.getExportedValue(); });
	}

	void ConfigWindow::invalidateShaderStatus() const
	{
		if (m_displayFilterSettingControl)
		{
			m_displayFilterSettingControl->invalidateShaderStatus();
		}
	}

	void ConfigWindow::onClose(ButtonControl& button)
	{
		static_cast<ConfigWindow*>(button.getParent())->setVisible(false);
	}

	void ConfigWindow::onExport(ButtonControl& button)
	{
		static_cast<ConfigWindow*>(button.getParent())->exportSettings();
	}

	void ConfigWindow::onImport(ButtonControl& button)
	{
		static_cast<ConfigWindow*>(button.getParent())->importSettings();
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

	void ConfigWindow::onResetAll(ButtonControl& button)
	{
		static_cast<ConfigWindow*>(button.getParent())->resetSettings();
	}

	void ConfigWindow::resetSettings()
	{
		updateSettings([](const Config::Setting& setting) { return setting.getBaseValue(); });
	}

	void ConfigWindow::setFocus(Control* control)
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
			addSettingControls();
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

		for (auto& settingRow : g_settingRows)
		{
			const auto& setting = *settingRow.setting;
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

	void ConfigWindow::updateDisplayFilter()
	{
		D3dDdi::ScopedCriticalSection lock;
		m_shaderParameters.clear();
		for (auto& device : D3dDdi::Device::getDevices())
		{
			auto& metaShader = device.second.getShaderBlitter().getMetaShader();
			metaShader.init();
			m_shaderParameters = metaShader.getParameters();
		}
		D3dDdi::MetaShader::clearUnusedBitmaps();
		m_scrollBar->setRange(0, Config::configRows.get().size() + m_shaderParameters.size() - 1);
	}

	void ConfigWindow::updateSettings(std::function<std::string(const Config::Setting&)> getValue)
	{
		D3dDdi::ScopedCriticalSection lock;
		for (auto& settingRow : g_settingRows)
		{
			const auto value = getValue(*settingRow.setting);
			if (value != settingRow.setting->getValueStr())
			{
				settingRow.setting->set(value, "overlay");
				if (settingRow.updateFunc)
				{
					settingRow.updateFunc();
				}
			}
		}

		addSettingControls();
		onMouseMove(Input::getRelativeCursorPos());
		updateButtons();
	}
}
