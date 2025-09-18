/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2023  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"

#include <QtWidgets/QInputDialog>
#include <QtWidgets/QMessageBox>
//#include <limits>

#include "pcsx2/Host.h"

//audio
//#include "pcsx2/SPU2/Global.h"
#include "pcsx2/Host/AudioStream.h"
#include "pcsx2/SPU2/spu2.h"
#include "pcsx2/VMManager.h"

#include "QuickSettingsWidget.h"
//#include "EmulationSettingsWidget.h"
//#include "GraphicsSettingsWidget.h"
//#include "AudioSettingsWidget.h"
#include "QtUtils.h"
#include "SettingWidgetBinder.h"
#include <pcsx2/mods/EmuPatches.h>
//#include "SettingsDialog.h"

static constexpr s32 DEFAULT_TARGET_LATENCY = 60;
static constexpr s32 DEFAULT_OUTPUT_LATENCY = 20;
static constexpr s32 DEFAULT_VOLUME = 100;
static constexpr u32 DEFAULT_FRAME_LATENCY = 2;
static constexpr s32 DEFAULT_SYNCHRONIZATION_MODE = 0;

QuickSettingsWidget::QuickSettingsWidget(SettingsWindow* dialog, QWidget* parent)
	: QWidget(parent)
	, m_dialog(dialog)
{
	SettingsInterface* sif = dialog->getSettingsInterface();

	m_ui.setupUi(this);

	//performance preset
	static const char* preset_entries[] = {"Max Performance", "Balanced", "Max Quality", "Custom", nullptr};
	static const char* preset_values[] = { "1", "2", "3", "0", nullptr};

	SettingWidgetBinder::BindWidgetToEnumSetting(
		sif, m_ui.perfPreset, "EmuCore", "performance_preset", preset_entries, preset_values, "0");
	connect(m_ui.perfPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QuickSettingsWidget::presetChanged);

	//internal resolution
	static const char* upscale_entries[] = {"Native (PS2) (Default)", "1.25x Native", "1.5x Native", "1.75x Native", "2x Native (~720p)",
		"2.25x Native", "2.5x Native", "2.75x Native", "3x Native (~1080p)", "3.5x Native", "4x Native (~1440p/2K)", "5x Native (~1620p)",
		"6x Native (~2160p/4K)", "7x Native (~2520p)", "8x Native (~2880p/5K)", nullptr};
	static const char* upscale_values[] = {
		"1", "1.25", "1.5", "1.75", "2", "2.25", "2.5", "2.75", "3", "3.5", "4", "5", "6", "7", "8", nullptr};
	SettingWidgetBinder::BindWidgetToEnumSetting(
		sif, m_ui.upscaleMultiplier, "EmuCore/GS", "upscale_multiplier", upscale_entries, upscale_values, "1.0");

	//bilinear filtering
	SettingWidgetBinder::BindWidgetToIntSetting(
		sif, m_ui.bilinearFiltering, "EmuCore/GS", "linear_present_mode", static_cast<int>(GSPostBilinearMode::BilinearSmooth));

	//Disable Interlacing
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_ui.noInterlacingPatches, "EmuCore", "EnableNoInterlacingPatches", true);
	connect(m_ui.noInterlacingPatches, &QCheckBox::checkStateChanged, this, &QuickSettingsWidget::onNoInterlacingChanged);

	//aspect ratio
	SettingWidgetBinder::BindWidgetToEnumSetting(
		sif, m_ui.aspectRatio, "EmuCore/GS", "AspectRatio", Pcsx2Config::GSOptions::AspectRatioNames, AspectRatioType::RAuto4_3_3_2);

	//vsync
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_ui.vsync, "EmuCore/GS", "VsyncEnable", 0);

	//optimal frame pacing
	connect(m_ui.optimalFramePacing, &QCheckBox::stateChanged, this, &QuickSettingsWidget::onOptimalFramePacingChanged);
	m_ui.optimalFramePacing->setTristate(dialog->isPerGameSettings());

	//audio output buffer
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_ui.bufferMS, "SPU2/Output", "BufferMS",
		AudioStreamParameters::DEFAULT_BUFFER_MS);
	//audio output latency
	SettingWidgetBinder::BindWidgetToIntSetting(sif, m_ui.outputLatencyMS, "SPU2/Output", "OutputLatencyMS",
		AudioStreamParameters::DEFAULT_OUTPUT_LATENCY_MS);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_ui.outputLatencyMinimal, "SPU2/Output", "OutputLatencyMinimal", false);
	//connect(m_ui.targetLatency, &QSlider::valueChanged, this, &QuickSettingsWidget::updateLatencyLabels);
	//connect(m_ui.outputLatency, &QSlider::valueChanged, this, &QuickSettingsWidget::updateLatencyLabels);

	connect(m_ui.bufferMS, &QSlider::valueChanged, this, &QuickSettingsWidget::updateLatencyLabel);
	connect(m_ui.outputLatencyMS, &QSlider::valueChanged, this, &QuickSettingsWidget::updateLatencyLabel);
	connect(m_ui.outputLatencyMinimal, &QCheckBox::checkStateChanged, this, &QuickSettingsWidget::onMinimalOutputLatencyChanged);
	onMinimalOutputLatencyChanged();
	updateLatencyLabel();

	//volume
	SettingWidgetBinder::BindWidgetAndLabelToIntSetting(sif, m_ui.volume, m_ui.volumeLabel, tr("%"), "SPU2/Output", "OutputVolume", 100);
	connect(m_ui.resetVolume, &QToolButton::clicked, this, [this]() { resetVolume(false); });

	dialog->registerWidgetHelp(m_ui.perfPreset, tr("Performance Preset"), tr("Custom"),
		tr("Adjusts multiple settings based on the selected preset. Max Performance and Balanced presets enable "
		   "Speedhacks, lowers Aniostrophic Filtering and Blending, and disables Optimal Frame Pacing."));

	dialog->registerWidgetHelp(m_ui.upscaleMultiplier, tr("Internal Resolution"), tr("Native (PS2) (Default)"),
		tr("Control the resolution at which games are rendered. High resolutions can impact performance on "
		   "older or lower-end GPUs.<br>Non-native resolution may cause minor graphical issues in some games.<br>"
		   "FMV resolution will remain unchanged, as the video files are pre-rendered."));

	dialog->registerWidgetHelp(m_ui.bilinearFiltering, tr("Bilinear Filtering"), tr("Bilinear (Smooth)"),
		tr("Enables bilinear post processing filter. Smooths the overall picture as it is displayed on the screen. Corrects "
		   "positioning between pixels."));

	dialog->registerWidgetHelp(m_ui.noInterlacingPatches, tr("Enable No-Interlacing Patch"), tr("Checked"),
		tr("Applies no-interlacing patches in PTR2, at the cost of halving the vertical resolution."));

	dialog->registerWidgetHelp(m_ui.aspectRatio, tr("Aspect Ratio"), tr("Auto Standard (4:3/3:2 Progressive)"),
		tr("Changes the aspect ratio used to display the console's output to the screen. The default is Auto Standard (4:3/3:2 "
		   "Progressive) which automatically adjusts the aspect ratio to match how a game would be shown on a typical TV of the era."));

	dialog->registerWidgetHelp(m_ui.vsync, tr("VSync"), tr("Unchecked"),
		tr("Enable this option to match PCSX2's refresh rate with your current monitor or screen. VSync is automatically disabled when "
		   "it is not possible (eg. running at non-100% speed)."));

	dialog->registerWidgetHelp(m_ui.optimalFramePacing, tr("Optimal Frame Pacing"), tr("Unchecked"),
		tr("Sets the VSync queue size to 0, making every frame be completed and presented by the GS before input is polled and the next frame begins. "
		   "Using this setting can reduce input lag at the cost of measurably higher CPU and GPU requirements."));

	dialog->registerWidgetHelp(
		m_ui.bufferMS, tr("Buffer Size"), tr("%1 ms").arg(AudioStreamParameters::DEFAULT_BUFFER_MS),
		tr("Determines the buffer size which the time stretcher will try to keep filled. It effectively selects the "
		   "average latency, as audio will be stretched/shrunk to keep the buffer size within check."));
	
	dialog->registerWidgetHelp(
		m_ui.outputLatencyMS, tr("Output Latency"), tr("%1 ms").arg(AudioStreamParameters::DEFAULT_OUTPUT_LATENCY_MS),
		tr("Determines the latency from the buffer to the host audio output. This can be set lower than the target latency "
		   "to reduce audio delay."));

	dialog->registerWidgetHelp(m_ui.volume, tr("Output Volume"), "100%",
		tr("Controls the volume of the audio played on the host."));

	dialog->registerWidgetHelp(m_ui.resetVolume, tr("Reset Volume"), tr("N/A"),
		m_dialog->isPerGameSettings() ? tr("Resets output volume back to the global/inherited setting.") :
                                        tr("Resets output volume back to the default."));

	//EmulationSettingsWidget::updateOptimalFramePacing();
}

QuickSettingsWidget::~QuickSettingsWidget() = default;

void QuickSettingsWidget::onNoInterlacingChanged()
{
	const bool enabled = m_dialog->getEffectiveBoolValue("EmuCore", "EnableNoInterlacingPatches", true);
	if (enabled)
		DisableInterlacing();
	else
		EnableInterlacing();
}

void QuickSettingsWidget::updateLatencyLabel()
{
	const u32 expand_buffer_ms = AudioStream::GetMSForBufferSize(SPU2::SAMPLE_RATE, getEffectiveExpansionBlockSize());
	const u32 config_buffer_ms = m_dialog->getEffectiveIntValue("SPU2/Output", "BufferMS", AudioStreamParameters::DEFAULT_BUFFER_MS);
	const u32 config_output_latency_ms = m_dialog->getEffectiveIntValue("SPU2/Output", "OutputLatencyMS", AudioStreamParameters::DEFAULT_OUTPUT_LATENCY_MS);
	const bool minimal_output = m_dialog->getEffectiveBoolValue("SPU2/Output", "OutputLatencyMinimal", false);

	//: Preserve the %1 variable, adapt the latter ms (and/or any possible spaces in between) to your language's ruleset.
	m_ui.outputLatencyLabel->setText(minimal_output ? tr("N/A") : tr("%1 ms").arg(config_output_latency_ms));
	m_ui.bufferMSLabel->setText(tr("%1 ms").arg(config_buffer_ms));

	const u32 output_latency_ms = minimal_output ? AudioStream::GetMSForBufferSize(SPU2::SAMPLE_RATE, m_output_device_latency) : config_output_latency_ms;
	if (output_latency_ms > 0)
	{
		if (expand_buffer_ms > 0)
		{
			m_ui.bufferingLabel->setText(tr("Maximum Latency: %1 ms (%2 ms buffer + %3 ms expand + %4 ms output)")
											 .arg(config_buffer_ms + expand_buffer_ms + output_latency_ms)
											 .arg(config_buffer_ms)
											 .arg(expand_buffer_ms)
											 .arg(output_latency_ms));
		}
		else
		{
			m_ui.bufferingLabel->setText(tr("Maximum Latency: %1 ms (%2 ms buffer + %3 ms output)")
											 .arg(config_buffer_ms + output_latency_ms)
											 .arg(config_buffer_ms)
											 .arg(output_latency_ms));
		}
	}
	else
	{
		if (expand_buffer_ms > 0)
		{
			m_ui.bufferingLabel->setText(tr("Maximum Latency: %1 ms (%2 ms expand, minimum output latency unknown)")
											 .arg(expand_buffer_ms + config_buffer_ms)
											 .arg(expand_buffer_ms));
		}
		else
		{
			m_ui.bufferingLabel->setText(tr("Maximum Latency: %1 ms (minimum output latency unknown)").arg(config_buffer_ms));
		}
	}
}

u32 QuickSettingsWidget::getEffectiveExpansionBlockSize() const
{
	const AudioExpansionMode expansion_mode = getEffectiveExpansionMode();
	if (expansion_mode == AudioExpansionMode::Disabled)
		return 0;

	const u32 config_block_size = m_dialog->getEffectiveIntValue("SPU2/Output", "ExpandBlockSize",
		AudioStreamParameters::DEFAULT_EXPAND_BLOCK_SIZE);
	return std::has_single_bit(config_block_size) ? config_block_size : std::bit_ceil(config_block_size);
}

AudioExpansionMode QuickSettingsWidget::getEffectiveExpansionMode() const
{
	return AudioStream::ParseExpansionMode(
		m_dialog->getEffectiveStringValue("SPU2/Output", "ExpansionMode",
					AudioStream::GetExpansionModeName(AudioStreamParameters::DEFAULT_EXPANSION_MODE))
			.c_str())
		.value_or(AudioStreamParameters::DEFAULT_EXPANSION_MODE);
}

void QuickSettingsWidget::updateVolumeLabel()
{
	m_ui.volumeLabel->setText(tr("%1%").arg(m_ui.volume->value()));
}

void QuickSettingsWidget::onMinimalOutputLatencyChanged()
{
	const bool minimal = m_dialog->getEffectiveBoolValue("SPU2/Output", "OutputLatencyMinimal", false);
	m_ui.outputLatencyMS->setEnabled(!minimal);
	updateLatencyLabel();
}

void QuickSettingsWidget::onOutputVolumeChanged(int new_value)
{
	// only called for base settings
	pxAssert(!m_dialog->isPerGameSettings());
	Host::SetBaseIntSettingValue("SPU2/Output", "OutputVolume", new_value);
	Host::CommitBaseSettingChanges();
	g_emu_thread->setAudioOutputVolume(new_value, EmuConfig.SPU2.FastForwardVolume);

	updateVolumeLabel();
}

void QuickSettingsWidget::resetVolume(bool fast_forward)
{
	const char* key = "OutputVolume";
	QSlider* const slider = m_ui.volume;
	QLabel* const label =  m_ui.volumeLabel;

	if (m_dialog->isPerGameSettings())
	{
		m_dialog->removeSettingValue("SPU2/Output", key);

		const int value = m_dialog->getEffectiveIntValue("SPU2/Output", key, 100);
		QSignalBlocker sb(slider);
		slider->setValue(value);
		label->setText(QStringLiteral("%1%2").arg(value).arg(tr("%")));

		// remove bold font if it was previously overridden
		QFont font(label->font());
		font.setBold(false);
		label->setFont(font);
	}
	else
	{
		slider->setValue(100);
	}
}

void QuickSettingsWidget::onOptimalFramePacingChanged()
{
	//const QSignalBlocker sb(m_ui.maxFrameLatency);

	std::optional<int> value;
	bool optimal = false;
	if (m_ui.optimalFramePacing->checkState() != Qt::PartiallyChecked)
	{
		optimal = m_ui.optimalFramePacing->isChecked();
		value = optimal ? 0 : DEFAULT_FRAME_LATENCY;
	}
	else
	{
		value = m_dialog->getEffectiveIntValue("EmuCore/GS", "VsyncQueueSize", DEFAULT_FRAME_LATENCY);
		optimal = (value == 0);
	}

	//m_ui.maxFrameLatency->setMinimum(optimal ? 0 : 1);
	//m_ui.maxFrameLatency->setValue(optimal ? 0 : DEFAULT_FRAME_LATENCY);
	//m_ui.maxFrameLatency->setEnabled(!m_dialog->isPerGameSettings() && !m_ui.optimalFramePacing->isChecked());
	if (value != m_dialog->getEffectiveIntValue("EmuCore/GS", "VsyncQueueSize", DEFAULT_FRAME_LATENCY))
		m_dialog->setStringSettingValue("EmuCore", "performance_preset", "0" );
	m_dialog->setIntSettingValue("EmuCore/GS", "VsyncQueueSize", value);

}

void QuickSettingsWidget::presetChanged()
{
	// int curval = m_ui.perfPreset->currentIndex();
	//could be a switch
	if (m_ui.perfPreset->currentIndex() == 2) // max quality
	{
		//affinity disabled
		m_dialog->setIntSettingValue("EmuCore/CPU", "AffinityControlMode", 0);
		//EE 100%
		m_dialog->setIntSettingValue("EmuCore/Speedhacks", "EECycleRate", 0);
		//EE Cycle Skipping Disabled
		m_dialog->setIntSettingValue("EmuCore/Speedhacks", "EECycleSkip", 0);
		//Optimal Frame ON
		m_dialog->setIntSettingValue("EmuCore/GS", "VsyncQueueSize", 0);
		// Blending Max
		m_dialog->setIntSettingValue("EmuCore/GS", "accurate_blending_unit", 5);

		// ani 16x
		m_dialog->setStringSettingValue("EmuCore/GS", "MaxAnisotropy", "16");
		// Mip FULL
		m_dialog->setIntSettingValue("EmuCore/GS", "mipmap_hw", 2);
		//Trilinear Forced
		m_dialog->setIntSettingValue("EmuCore/GS", "TriFilter", 2);
	}
	else if (m_ui.perfPreset->currentIndex() == 1) // balanced
	{
		//affinity disabled
		m_dialog->setIntSettingValue("EmuCore/CPU", "AffinityControlMode", 0);
		//EE 75% under
		m_dialog->setIntSettingValue("EmuCore/Speedhacks", "EECycleRate", -1);
		//EE Cycle Skipping Disabled
		m_dialog->setIntSettingValue("EmuCore/Speedhacks", "EECycleSkip", 0);
		//Optimal Frame Off
		if (DEFAULT_FRAME_LATENCY < 2)
		{
			m_dialog->setIntSettingValue("EmuCore/GS", "VsyncQueueSize", 2);
		}
		else
		{
			m_dialog->setIntSettingValue("EmuCore/GS", "VsyncQueueSize", DEFAULT_FRAME_LATENCY);
		}
		// Blending medium
		m_dialog->setIntSettingValue("EmuCore/GS", "accurate_blending_unit", 2);
		// ani 8x
		m_dialog->setStringSettingValue("EmuCore/GS", "MaxAnisotropy", "8");
		// Mip FULL
		m_dialog->setIntSettingValue("EmuCore/GS", "mipmap_hw", 2);
		//Trilinear Forced
		m_dialog->setIntSettingValue("EmuCore/GS", "TriFilter", 2);
	}
	else if (m_ui.perfPreset->currentIndex() == 0) // max performance
	{
		//affinity VU EE GS
		m_dialog->setIntSettingValue("EmuCore/CPU", "AffinityControlMode", 3);
		//EE 60% under
		m_dialog->setIntSettingValue("EmuCore/Speedhacks", "EECycleRate", -2);
		//EE Cycle Skipping Disabled
		m_dialog->setIntSettingValue("EmuCore/Speedhacks", "EECycleSkip", 0);
		//Optimal Frame Off
		if (DEFAULT_FRAME_LATENCY < 2)
		{
			m_dialog->setIntSettingValue("EmuCore/GS", "VsyncQueueSize", 2);
		}
		else
		{
			m_dialog->setIntSettingValue("EmuCore/GS", "VsyncQueueSize", DEFAULT_FRAME_LATENCY);
		}
		// Blending Minimum
		m_dialog->setIntSettingValue("EmuCore/GS", "accurate_blending_unit", 0);

		// ani OFF
		m_dialog->setStringSettingValue("EmuCore/GS", "MaxAnisotropy", "0");
		// Mip FULL
		m_dialog->setIntSettingValue("EmuCore/GS", "mipmap_hw", 2);
		//Trilinear Forced
		m_dialog->setIntSettingValue("EmuCore/GS", "TriFilter", 2);

	}

}