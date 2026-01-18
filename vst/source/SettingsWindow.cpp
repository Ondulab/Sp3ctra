#include "SettingsWindow.h"
#include "Sp3ctraConstants.h"

//==============================================================================
// SettingsComponent Implementation
//==============================================================================

SettingsComponent::SettingsComponent(Sp3ctraAudioProcessor& processor)
    : audioProcessor(processor),
      tabbedComponent(juce::TabbedButtonBar::TabsAtTop)
{
    // Create and add tabs
    generalTab = new GeneralSettingsTab(processor);
    networkTab = new NetworkSettingsTab(processor);
    luxstralTab = new LuxStralSettingsTab(processor);
    
    tabbedComponent.addTab("General", juce::Colours::darkgrey, generalTab, false);
    tabbedComponent.addTab("Network", juce::Colours::darkgrey, networkTab, false);
    tabbedComponent.addTab("LuxStral", juce::Colours::darkgrey, luxstralTab, false);
    
    addAndMakeVisible(tabbedComponent);
    
    setSize(550, 450);
}

SettingsComponent::~SettingsComponent()
{
    // Tabs are owned and deleted by TabbedComponent
}

void SettingsComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    // Title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    g.drawText("Sp3ctra Configuration", getLocalBounds().removeFromTop(40),
               juce::Justification::centred, true);
}

void SettingsComponent::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(40);  // Skip title area
    bounds.reduce(10, 5);
    
    tabbedComponent.setBounds(bounds);
}

//==============================================================================
// SettingsWindow Implementation
//==============================================================================

SettingsWindow::SettingsWindow(Sp3ctraAudioProcessor& processor)
    : DocumentWindow("Sp3ctra Settings",
                     juce::Desktop::getInstance().getDefaultLookAndFeel()
                         .findColour(juce::ResizableWindow::backgroundColourId),
                     DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new SettingsComponent(processor), true);

    #if JUCE_IOS || JUCE_ANDROID
        setFullScreen(true);
    #else
        setResizable(false, false);
        centreWithSize(getWidth(), getHeight());
    #endif

    setVisible(true);
}

SettingsWindow::~SettingsWindow()
{
}

void SettingsWindow::closeButtonPressed()
{
    // Just hide the window, don't delete it
    setVisible(false);
}
