#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class Sp3ctraAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    Sp3ctraAudioProcessorEditor (Sp3ctraAudioProcessor&);
    ~Sp3ctraAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    Sp3ctraAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sp3ctraAudioProcessorEditor)
};
