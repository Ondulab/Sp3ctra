#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../PluginProcessor.h"

//==============================================================================
/**
 * @brief General Settings Tab
 * 
 * Contains:
 * - Visualizer Mode (Image/Waveform/Inverted)
 * - Log Level (Error/Warning/Info/Debug)
 */
class GeneralSettingsTab : public juce::Component
{
public:
    GeneralSettingsTab(Sp3ctraAudioProcessor& processor);
    ~GeneralSettingsTab() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    Sp3ctraAudioProcessor& audioProcessor;
    juce::AudioProcessorValueTreeState& apvts;

    // Visualizer Mode
    juce::Label visualizerModeLabel;
    juce::ComboBox visualizerModeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> visualizerModeAttachment;

    // Log Level
    juce::Label logLevelLabel;
    juce::ComboBox logLevelCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> logLevelAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GeneralSettingsTab)
};
