#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../PluginProcessor.h"

//==============================================================================
/**
 * @brief Network Settings Tab
 * 
 * Contains:
 * - UDP Port
 * - UDP Address (4 bytes)
 * - Sensor DPI (200/400)
 * - Apply Button + Status
 */
class NetworkSettingsTab : public juce::Component
{
public:
    NetworkSettingsTab(Sp3ctraAudioProcessor& processor);
    ~NetworkSettingsTab() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    Sp3ctraAudioProcessor& audioProcessor;
    juce::AudioProcessorValueTreeState& apvts;

    // UDP Port
    juce::Label udpPortLabel;
    juce::TextEditor udpPortEditor;
    
    // UDP Address (4 bytes)
    juce::Label udpAddressLabel;
    juce::TextEditor udpByte1Editor;
    juce::TextEditor udpByte2Editor;
    juce::TextEditor udpByte3Editor;
    juce::TextEditor udpByte4Editor;
    juce::Label dot1Label, dot2Label, dot3Label;

    // Sensor DPI
    juce::Label sensorDpiLabel;
    juce::ComboBox sensorDpiCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sensorDpiAttachment;

    // Apply Button & Status
    juce::TextButton applyButton;
    juce::Label statusLabel;

    void applyChanges();
    void updateStatusLabel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NetworkSettingsTab)
};
