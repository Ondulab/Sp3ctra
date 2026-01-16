#include "SettingsWindow.h"
#include "Sp3ctraConstants.h"

//==============================================================================
// SettingsComponent Implementation
//==============================================================================

SettingsComponent::SettingsComponent(Sp3ctraAudioProcessor& processor)
    : audioProcessor(processor),
      apvts(processor.getAPVTS())
{
    // UDP Port - TextEditor (editable field)
    udpPortLabel.setText("UDP Port:", juce::dontSendNotification);
    udpPortLabel.setJustificationType(juce::Justification::centredRight);
    udpPortLabel.setFont(juce::FontOptions(14.0f));
    addAndMakeVisible(udpPortLabel);

    udpPortEditor.setMultiLine(false);
    udpPortEditor.setReturnKeyStartsNewLine(false);
    udpPortEditor.setReadOnly(false);
    udpPortEditor.setScrollbarsShown(false);
    udpPortEditor.setCaretVisible(true);
    udpPortEditor.setPopupMenuEnabled(true);
    udpPortEditor.setFont(juce::FontOptions(14.0f));
    udpPortEditor.setJustification(juce::Justification::centred);
    udpPortEditor.setInputRestrictions(5, "0123456789");
    
    // Initialize with current APVTS value
    int currentPort = (int)apvts.getRawParameterValue("udpPort")->load();
    udpPortEditor.setText(juce::String(currentPort), false);
    
    // Update APVTS when text changes
    udpPortEditor.onReturnKey = [this]() {
        int port = udpPortEditor.getText().getIntValue();
        if (port >= 1024 && port <= 65535) {
            apvts.getParameter("udpPort")->setValueNotifyingHost(
                apvts.getParameter("udpPort")->convertTo0to1(port));
        } else {
            int currentPort = (int)apvts.getRawParameterValue("udpPort")->load();
            udpPortEditor.setText(juce::String(currentPort), false);
        }
    };
    
    udpPortEditor.onFocusLost = [this]() {
        udpPortEditor.onReturnKey();
    };
    
    addAndMakeVisible(udpPortEditor);

    // UDP Address - 4 TextEditors for IP bytes
    udpAddressLabel.setText("UDP Address:", juce::dontSendNotification);
    udpAddressLabel.setJustificationType(juce::Justification::centredRight);
    udpAddressLabel.setFont(juce::FontOptions(14.0f));
    addAndMakeVisible(udpAddressLabel);

    // Helper lambda to configure IP byte editor
    auto setupByteEditor = [this](juce::TextEditor& editor, const char* paramName) {
        editor.setMultiLine(false);
        editor.setReturnKeyStartsNewLine(false);
        editor.setReadOnly(false);
        editor.setScrollbarsShown(false);
        editor.setCaretVisible(true);
        editor.setPopupMenuEnabled(true);
        editor.setFont(juce::FontOptions(14.0f));
        editor.setJustification(juce::Justification::centred);
        editor.setInputRestrictions(3, "0123456789");
        
        // Initialize with current value
        int currentValue = (int)apvts.getRawParameterValue(paramName)->load();
        editor.setText(juce::String(currentValue), false);
        
        // Update APVTS when value changes
        editor.onReturnKey = [this, &editor, paramName]() {
            int value = editor.getText().getIntValue();
            if (value >= 0 && value <= 255) {
                apvts.getParameter(paramName)->setValueNotifyingHost(
                    apvts.getParameter(paramName)->convertTo0to1(value));
            } else {
                int current = (int)apvts.getRawParameterValue(paramName)->load();
                editor.setText(juce::String(current), false);
            }
        };
        
        editor.onFocusLost = [&editor]() {
            editor.onReturnKey();
        };
        
        addAndMakeVisible(editor);
    };

    // Setup 4 byte editors
    setupByteEditor(udpByte1Editor, "udpByte1");
    setupByteEditor(udpByte2Editor, "udpByte2");
    setupByteEditor(udpByte3Editor, "udpByte3");
    setupByteEditor(udpByte4Editor, "udpByte4");
    
    // Dot labels
    dot1Label.setText(".", juce::dontSendNotification);
    dot1Label.setJustificationType(juce::Justification::centred);
    dot1Label.setFont(juce::Font(juce::FontOptions(16.0f)).boldened());
    addAndMakeVisible(dot1Label);
    
    dot2Label.setText(".", juce::dontSendNotification);
    dot2Label.setJustificationType(juce::Justification::centred);
    dot2Label.setFont(juce::Font(juce::FontOptions(16.0f)).boldened());
    addAndMakeVisible(dot2Label);
    
    dot3Label.setText(".", juce::dontSendNotification);
    dot3Label.setJustificationType(juce::Justification::centred);
    dot3Label.setFont(juce::Font(juce::FontOptions(16.0f)).boldened());
    addAndMakeVisible(dot3Label);

    // Sensor DPI
    sensorDpiLabel.setText("Sensor DPI:", juce::dontSendNotification);
    sensorDpiLabel.setJustificationType(juce::Justification::centredRight);
    sensorDpiLabel.setFont(juce::FontOptions(14.0f));
    addAndMakeVisible(sensorDpiLabel);

    sensorDpiCombo.addItem("200 DPI (1728 pixels)", 1);
    sensorDpiCombo.addItem("400 DPI (3456 pixels)", 2);
    addAndMakeVisible(sensorDpiCombo);

    sensorDpiAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, "sensorDpi", sensorDpiCombo);

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

    // Apply Button (functional - saves all fields)
    applyButton.setButtonText("Apply Settings");
    applyButton.setEnabled(true);
    applyButton.onClick = [this]() { applyChanges(); };
    addAndMakeVisible(applyButton);

    // Status Label
    statusLabel.setText("Settings are saved automatically", 
                       juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setFont(juce::Font(juce::FontOptions(12.0f)).italicised());
    addAndMakeVisible(statusLabel);

    setSize(500, 320);  // +40px for visualizer mode row
}

SettingsComponent::~SettingsComponent()
{
}

void SettingsComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    // Title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    g.drawText("Sp3ctra Configuration", getLocalBounds().removeFromTop(40),
               juce::Justification::centred, true);

    // Section separator
    g.setColour(juce::Colours::grey);
    g.drawLine(20, 40, getWidth() - 20, 40, 1.0f);
}

void SettingsComponent::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(50);  // Skip title area
    bounds.reduce(20, 10);

    const int labelWidth = 120;
    const int rowHeight = 35;
    const int padding = 10;

    // UDP Port
    auto portRow = bounds.removeFromTop(rowHeight);
    udpPortLabel.setBounds(portRow.removeFromLeft(labelWidth));
    portRow.removeFromLeft(padding);
    udpPortEditor.setBounds(portRow);

    bounds.removeFromTop(5);

    // UDP Address - 4 fields with dots
    auto addressRow = bounds.removeFromTop(rowHeight);
    udpAddressLabel.setBounds(addressRow.removeFromLeft(labelWidth));
    addressRow.removeFromLeft(padding);
    
    int byteWidth = (addressRow.getWidth() - 30) / 4;  // Space for 3 dots
    udpByte1Editor.setBounds(addressRow.removeFromLeft(byteWidth));
    dot1Label.setBounds(addressRow.removeFromLeft(10));
    udpByte2Editor.setBounds(addressRow.removeFromLeft(byteWidth));
    dot2Label.setBounds(addressRow.removeFromLeft(10));
    udpByte3Editor.setBounds(addressRow.removeFromLeft(byteWidth));
    dot3Label.setBounds(addressRow.removeFromLeft(10));
    udpByte4Editor.setBounds(addressRow.removeFromLeft(byteWidth));

    bounds.removeFromTop(5);

    // Sensor DPI
    auto dpiRow = bounds.removeFromTop(rowHeight);
    sensorDpiLabel.setBounds(dpiRow.removeFromLeft(labelWidth));
    dpiRow.removeFromLeft(padding);
    sensorDpiCombo.setBounds(dpiRow);

    bounds.removeFromTop(5);

    // Log Level
    auto logRow = bounds.removeFromTop(rowHeight);
    logLevelLabel.setBounds(logRow.removeFromLeft(labelWidth));
    logRow.removeFromLeft(padding);
    logLevelCombo.setBounds(logRow);

    bounds.removeFromTop(5);

    // Visualizer Mode
    auto vizRow = bounds.removeFromTop(rowHeight);
    visualizerModeLabel.setBounds(vizRow.removeFromLeft(labelWidth));
    vizRow.removeFromLeft(padding);
    visualizerModeCombo.setBounds(vizRow);

    bounds.removeFromTop(15);

    // Apply Button
    applyButton.setBounds(bounds.removeFromTop(30).reduced(50, 0));

    bounds.removeFromTop(10);

    // Status Label
    statusLabel.setBounds(bounds.removeFromTop(20));
}

void SettingsComponent::applyChanges()
{
    // Force apply all TextEditor values to APVTS
    
    // UDP Port
    udpPortEditor.onReturnKey();
    
    // UDP Address bytes
    udpByte1Editor.onReturnKey();
    udpByte2Editor.onReturnKey();
    udpByte3Editor.onReturnKey();
    udpByte4Editor.onReturnKey();
    
    // Update status
    updateStatusLabel();
    
    // Visual feedback
    applyButton.setButtonText("Settings Applied!");
    juce::Timer::callAfterDelay(1500, [this]() {
        applyButton.setButtonText("Apply Settings");
    });
}

void SettingsComponent::updateStatusLabel()
{
    auto* core = audioProcessor.getSp3ctraCore();
    if (core && core->isInitialized()) {
        statusLabel.setText("Configuration active", juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, juce::Colours::green);
    } else {
        statusLabel.setText("Configuration error", juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, juce::Colours::red);
    }
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
