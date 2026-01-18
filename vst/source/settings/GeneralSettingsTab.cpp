#include "GeneralSettingsTab.h"
#include "../Sp3ctraConstants.h"

//==============================================================================
GeneralSettingsTab::GeneralSettingsTab(Sp3ctraAudioProcessor& processor)
    : audioProcessor(processor),
      apvts(processor.getAPVTS())
{
    // Visualizer Mode
    visualizerModeLabel.setText("Visualizer Mode:", juce::dontSendNotification);
    visualizerModeLabel.setJustificationType(juce::Justification::centredRight);
    visualizerModeLabel.setFont(juce::FontOptions(14.0f));
    addAndMakeVisible(visualizerModeLabel);

    visualizerModeCombo.addItem("Image", 1);
    visualizerModeCombo.addItem("Waveform", 2);
    visualizerModeCombo.addItem("Inverted Waveform", 3);
    addAndMakeVisible(visualizerModeCombo);

    visualizerModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, "visualizerMode", visualizerModeCombo);

    // Log Level
    logLevelLabel.setText("Log Level:", juce::dontSendNotification);
    logLevelLabel.setJustificationType(juce::Justification::centredRight);
    logLevelLabel.setFont(juce::FontOptions(14.0f));
    addAndMakeVisible(logLevelLabel);

    logLevelCombo.addItem("Error", 1);
    logLevelCombo.addItem("Warning", 2);
    logLevelCombo.addItem("Info", 3);
    logLevelCombo.addItem("Debug", 4);
    addAndMakeVisible(logLevelCombo);

    logLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, "logLevel", logLevelCombo);
}

GeneralSettingsTab::~GeneralSettingsTab()
{
}

void GeneralSettingsTab::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    // Section title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(16.0f)).boldened());
    g.drawText("General Configuration", getLocalBounds().removeFromTop(30),
               juce::Justification::centred, true);
}

void GeneralSettingsTab::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(40);  // Skip title area
    bounds.reduce(20, 10);

    const int labelWidth = 120;
    const int rowHeight = 35;
    const int padding = 10;

    // Visualizer Mode
    auto vizRow = bounds.removeFromTop(rowHeight);
    visualizerModeLabel.setBounds(vizRow.removeFromLeft(labelWidth));
    vizRow.removeFromLeft(padding);
    visualizerModeCombo.setBounds(vizRow);

    bounds.removeFromTop(5);

    // Log Level
    auto logRow = bounds.removeFromTop(rowHeight);
    logLevelLabel.setBounds(logRow.removeFromLeft(labelWidth));
    logRow.removeFromLeft(padding);
    logLevelCombo.setBounds(logRow);
}
