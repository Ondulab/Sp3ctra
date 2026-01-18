#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "SettingsWindow.h"
#include "CisVisualizerComponent.h"

//==============================================================================
/**
 * @brief Main VST Editor
 * 
 * Minimalist interface with:
 * - Settings button to configure parameters
 * - Status display
 */
class Sp3ctraAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    Sp3ctraAudioProcessorEditor (Sp3ctraAudioProcessor&);
    ~Sp3ctraAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    
    // Suspend/resume visualizer (protects against graphics race conditions during buffer size changes)
    void suspendVisualizer();
    void resumeVisualizer();

private:
    void timerCallback() override;
    void openSettings();

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    Sp3ctraAudioProcessor& audioProcessor;

    // UI Components
    juce::TextButton settingsButton;
    juce::Label statusLabel;
    juce::Label infoLabel;
    std::unique_ptr<CisVisualizerComponent> cisVisualizer;

    // Settings window (created on demand)
    std::unique_ptr<SettingsWindow> settingsWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sp3ctraAudioProcessorEditor)
};
