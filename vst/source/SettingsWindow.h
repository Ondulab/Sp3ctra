#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

//==============================================================================
/**
 * @brief Settings window for Sp3ctra VST parameters
 * 
 * Provides a clean UI to configure:
 * - UDP network settings (port, address)
 * - Sensor DPI (200 or 400)
 * - Log level
 * 
 * All changes are automatically saved via APVTS to DAW projects.
 */
class SettingsComponent : public juce::Component
{
public:
    SettingsComponent(Sp3ctraAudioProcessor& processor);
    ~SettingsComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    Sp3ctraAudioProcessor& audioProcessor;
    juce::AudioProcessorValueTreeState& apvts;

    // UI Components
    juce::Label udpPortLabel;
    juce::TextEditor udpPortEditor;
    
    juce::Label udpAddressLabel;
    juce::TextEditor udpByte1Editor;
    juce::TextEditor udpByte2Editor;
    juce::TextEditor udpByte3Editor;
    juce::TextEditor udpByte4Editor;
    juce::Label dot1Label, dot2Label, dot3Label;

    juce::Label sensorDpiLabel;
    juce::ComboBox sensorDpiCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sensorDpiAttachment;

    juce::Label logLevelLabel;
    juce::ComboBox logLevelCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> logLevelAttachment;

    juce::Label visualizerModeLabel;
    juce::ComboBox visualizerModeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> visualizerModeAttachment;

    juce::TextButton applyButton;
    juce::Label statusLabel;

    void applyChanges();
    void updateStatusLabel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
};

//==============================================================================
/**
 * @brief Settings window wrapper
 * 
 * A DocumentWindow that contains the SettingsComponent.
 * Can be shown/hidden without destroying the component.
 */
class SettingsWindow : public juce::DocumentWindow
{
public:
    SettingsWindow(Sp3ctraAudioProcessor& processor);
    ~SettingsWindow() override;

    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsWindow)
};
