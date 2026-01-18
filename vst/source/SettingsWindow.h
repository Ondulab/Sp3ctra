#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "settings/GeneralSettingsTab.h"
#include "settings/NetworkSettingsTab.h"
#include "settings/LuxStralSettingsTab.h"

//==============================================================================
/**
 * @brief Settings window for Sp3ctra VST parameters
 * 
 * Organized in 3 tabs:
 * - General: Visualizer Mode, Log Level
 * - Network: UDP configuration, Sensor DPI
 * - LuxStral: Additive synthesis parameters
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

    // Tabbed interface
    juce::TabbedComponent tabbedComponent;
    
    // Tab content (owned by TabbedComponent)
    GeneralSettingsTab* generalTab;
    NetworkSettingsTab* networkTab;
    LuxStralSettingsTab* luxstralTab;

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
