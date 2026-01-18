#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../PluginProcessor.h"

//==============================================================================
/**
 * @brief LuxStral Synthesis Settings Tab
 * 
 * Contains all parameters for the additive synthesis engine:
 * - Frequency Range
 * - Envelope Parameters
 * - Image Processing
 * - Stereo Processing
 * - Dynamics Processing
 * - Performance
 */
class LuxStralSettingsTab : public juce::Component
{
public:
    LuxStralSettingsTab(Sp3ctraAudioProcessor& processor);
    ~LuxStralSettingsTab() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    Sp3ctraAudioProcessor& audioProcessor;
    juce::AudioProcessorValueTreeState& apvts;

    // Viewport for scrolling
    juce::Viewport viewport;
    juce::Component contentComponent;

    // Section: Frequency Range
    juce::Label freqRangeSectionLabel;
    juce::Label lowFreqLabel;
    juce::Slider lowFreqSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowFreqAttachment;
    juce::Label highFreqLabel;
    juce::Slider highFreqSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highFreqAttachment;

    // Section: Envelope Parameters
    juce::Label envelopeSectionLabel;
    juce::Label attackLabel;
    juce::Slider attackSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;
    juce::Label releaseLabel;
    juce::Slider releaseSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment;

    // Section: Image Processing
    juce::Label imageProcSectionLabel;
    juce::Label gammaEnableLabel;
    juce::ToggleButton gammaEnableToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> gammaEnableAttachment;
    juce::Label gammaValueLabel;
    juce::Slider gammaValueSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gammaValueAttachment;
    juce::Label contrastMinLabel;
    juce::Slider contrastMinSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> contrastMinAttachment;

    // Section: Stereo Processing
    juce::Label stereoSectionLabel;
    juce::Label stereoEnableLabel;
    juce::ToggleButton stereoEnableToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stereoEnableAttachment;
    juce::Label stereoTempAmpLabel;
    juce::Slider stereoTempAmpSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> stereoTempAmpAttachment;

    // Section: Dynamics Processing
    juce::Label dynamicsSectionLabel;
    juce::Label volumeWeightingLabel;
    juce::Slider volumeWeightingSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeWeightingAttachment;
    juce::Label softLimitThresholdLabel;
    juce::Slider softLimitThresholdSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> softLimitThresholdAttachment;
    juce::Label softLimitKneeLabel;
    juce::Slider softLimitKneeSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> softLimitKneeAttachment;

    // Section: Performance
    juce::Label performanceSectionLabel;
    juce::Label numWorkersLabel;
    juce::Slider numWorkersSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> numWorkersAttachment;

    void layoutContentComponent();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LuxStralSettingsTab)
};
