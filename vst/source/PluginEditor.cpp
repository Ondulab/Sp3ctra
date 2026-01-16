#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
Sp3ctraAudioProcessorEditor::Sp3ctraAudioProcessorEditor (Sp3ctraAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Settings Button (no emoji to avoid encoding issues)
    settingsButton.setButtonText("Settings");
    settingsButton.onClick = [this] { openSettings(); };
    addAndMakeVisible(settingsButton);

    // Status Label
    statusLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    addAndMakeVisible(statusLabel);

    // Info Label
    infoLabel.setText("Sp3ctra - Spectral Audio Synthesis\nUDP Receiver Active", 
                     juce::dontSendNotification);
    infoLabel.setJustificationType(juce::Justification::centred);
    infoLabel.setFont(juce::Font(12.0f));
    addAndMakeVisible(infoLabel);

    // CIS Visualizer
    cisVisualizer = std::make_unique<CisVisualizerComponent>(audioProcessor);
    addAndMakeVisible(cisVisualizer.get());

    // Start timer to update status (1 Hz)
    startTimer(1000);

    setSize (400, 320);
}

Sp3ctraAudioProcessorEditor::~Sp3ctraAudioProcessorEditor()
{
    stopTimer();
    settingsWindow.reset();
}

//==============================================================================
void Sp3ctraAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Background gradient
    g.fillAll(juce::Colour(0xff2a2a2a));
    
    auto bounds = getLocalBounds();
    
    // Header section
    auto headerArea = bounds.removeFromTop(60);
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(0xff404040), 0, 0,
        juce::Colour(0xff2a2a2a), 0, (float)headerArea.getHeight(),
        false));
    g.fillRect(headerArea);

    // Title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(20.0f, juce::Font::bold));
    g.drawText("Sp3ctra", headerArea.reduced(10), juce::Justification::centredLeft, true);

    // Version
    g.setFont(juce::Font(11.0f));
    g.setColour(juce::Colours::grey);
    g.drawText("v0.0.1", headerArea.reduced(10), juce::Justification::centredRight, true);
}

void Sp3ctraAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(70);  // Skip header
    bounds.reduce(20, 10);

    // CIS Visualizer (horizontal elongated format)
    auto visualizerArea = bounds.removeFromTop(80);
    cisVisualizer->setBounds(visualizerArea);

    bounds.removeFromTop(10);  // Spacing

    // Settings button
    settingsButton.setBounds(bounds.removeFromTop(40).reduced(80, 0));

    bounds.removeFromTop(20);

    // Status label
    statusLabel.setBounds(bounds.removeFromTop(30));

    bounds.removeFromTop(10);

    // Info label
    infoLabel.setBounds(bounds.removeFromTop(60));
}

void Sp3ctraAudioProcessorEditor::timerCallback()
{
    // Update status based on core state
    auto* core = audioProcessor.getSp3ctraCore();
    
    if (core && core->isInitialized()) {
        auto& apvts = audioProcessor.getAPVTS();
        int port = (int)apvts.getRawParameterValue("udpPort")->load();
        juce::String address = audioProcessor.getUdpAddressString();
        
        statusLabel.setText("UDP Active: " + address + ":" + juce::String(port), 
                           juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
    } else {
        statusLabel.setText("Configuration Error", juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    }
}

void Sp3ctraAudioProcessorEditor::openSettings()
{
    if (!settingsWindow) {
        settingsWindow = std::make_unique<SettingsWindow>(audioProcessor);
    } else {
        settingsWindow->setVisible(true);
        settingsWindow->toFront(true);
    }
}
