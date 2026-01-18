#include "LuxStralSettingsTab.h"
#include "../Sp3ctraConstants.h"

//==============================================================================
LuxStralSettingsTab::LuxStralSettingsTab(Sp3ctraAudioProcessor& processor)
    : audioProcessor(processor),
      apvts(processor.getAPVTS())
{
    // Setup viewport for scrolling
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&contentComponent, false);
    viewport.setScrollBarsShown(true, false);

    // ========================================================================
    // Section: Frequency Range
    // ========================================================================
    freqRangeSectionLabel.setText("Frequency Range", juce::dontSendNotification);
    freqRangeSectionLabel.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    freqRangeSectionLabel.setColour(juce::Label::textColourId, juce::Colours::lightblue);
    contentComponent.addAndMakeVisible(freqRangeSectionLabel);

    lowFreqLabel.setText("Low Frequency:", juce::dontSendNotification);
    lowFreqLabel.setJustificationType(juce::Justification::centredRight);
    contentComponent.addAndMakeVisible(lowFreqLabel);
    
    lowFreqSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    lowFreqSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    lowFreqSlider.setTextValueSuffix(" Hz");
    contentComponent.addAndMakeVisible(lowFreqSlider);
    lowFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "luxstralLowFreq", lowFreqSlider);

    highFreqLabel.setText("High Frequency:", juce::dontSendNotification);
    highFreqLabel.setJustificationType(juce::Justification::centredRight);
    contentComponent.addAndMakeVisible(highFreqLabel);
    
    highFreqSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    highFreqSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    highFreqSlider.setTextValueSuffix(" Hz");
    contentComponent.addAndMakeVisible(highFreqSlider);
    highFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "luxstralHighFreq", highFreqSlider);

    // ========================================================================
    // Section: Envelope Parameters
    // ========================================================================
    envelopeSectionLabel.setText("Envelope Parameters", juce::dontSendNotification);
    envelopeSectionLabel.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    envelopeSectionLabel.setColour(juce::Label::textColourId, juce::Colours::lightblue);
    contentComponent.addAndMakeVisible(envelopeSectionLabel);

    attackLabel.setText("Attack Time:", juce::dontSendNotification);
    attackLabel.setJustificationType(juce::Justification::centredRight);
    contentComponent.addAndMakeVisible(attackLabel);
    
    attackSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    attackSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    attackSlider.setTextValueSuffix(" ms");
    contentComponent.addAndMakeVisible(attackSlider);
    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "luxstralAttackMs", attackSlider);

    releaseLabel.setText("Release Time:", juce::dontSendNotification);
    releaseLabel.setJustificationType(juce::Justification::centredRight);
    contentComponent.addAndMakeVisible(releaseLabel);
    
    releaseSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    releaseSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    releaseSlider.setTextValueSuffix(" ms");
    contentComponent.addAndMakeVisible(releaseSlider);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "luxstralReleaseMs", releaseSlider);

    // ========================================================================
    // Section: Image Processing
    // ========================================================================
    imageProcSectionLabel.setText("Image Processing", juce::dontSendNotification);
    imageProcSectionLabel.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    imageProcSectionLabel.setColour(juce::Label::textColourId, juce::Colours::lightblue);
    contentComponent.addAndMakeVisible(imageProcSectionLabel);

    gammaEnableLabel.setText("Gamma Correction:", juce::dontSendNotification);
    gammaEnableLabel.setJustificationType(juce::Justification::centredRight);
    contentComponent.addAndMakeVisible(gammaEnableLabel);
    
    gammaEnableToggle.setButtonText("Enable");
    contentComponent.addAndMakeVisible(gammaEnableToggle);
    gammaEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, "luxstralGammaEnable", gammaEnableToggle);

    gammaValueLabel.setText("Gamma Value:", juce::dontSendNotification);
    gammaValueLabel.setJustificationType(juce::Justification::centredRight);
    contentComponent.addAndMakeVisible(gammaValueLabel);
    
    gammaValueSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    gammaValueSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    contentComponent.addAndMakeVisible(gammaValueSlider);
    gammaValueAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "luxstralGammaValue", gammaValueSlider);

    contrastMinLabel.setText("Contrast Min:", juce::dontSendNotification);
    contrastMinLabel.setJustificationType(juce::Justification::centredRight);
    contentComponent.addAndMakeVisible(contrastMinLabel);
    
    contrastMinSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    contrastMinSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    contentComponent.addAndMakeVisible(contrastMinSlider);
    contrastMinAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "luxstralContrastMin", contrastMinSlider);

    // ========================================================================
    // Section: Stereo Processing
    // ========================================================================
    stereoSectionLabel.setText("Stereo Processing", juce::dontSendNotification);
    stereoSectionLabel.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    stereoSectionLabel.setColour(juce::Label::textColourId, juce::Colours::lightblue);
    contentComponent.addAndMakeVisible(stereoSectionLabel);

    stereoEnableLabel.setText("Stereo Mode:", juce::dontSendNotification);
    stereoEnableLabel.setJustificationType(juce::Justification::centredRight);
    contentComponent.addAndMakeVisible(stereoEnableLabel);
    
    stereoEnableToggle.setButtonText("Enable");
    contentComponent.addAndMakeVisible(stereoEnableToggle);
    stereoEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, "luxstralStereoEnable", stereoEnableToggle);

    stereoTempAmpLabel.setText("Temperature Amp:", juce::dontSendNotification);
    stereoTempAmpLabel.setJustificationType(juce::Justification::centredRight);
    contentComponent.addAndMakeVisible(stereoTempAmpLabel);
    
    stereoTempAmpSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    stereoTempAmpSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    contentComponent.addAndMakeVisible(stereoTempAmpSlider);
    stereoTempAmpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "luxstralStereoTempAmp", stereoTempAmpSlider);

    // ========================================================================
    // Section: Dynamics Processing
    // ========================================================================
    dynamicsSectionLabel.setText("Dynamics Processing", juce::dontSendNotification);
    dynamicsSectionLabel.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    dynamicsSectionLabel.setColour(juce::Label::textColourId, juce::Colours::lightblue);
    contentComponent.addAndMakeVisible(dynamicsSectionLabel);

    volumeWeightingLabel.setText("Volume Weighting:", juce::dontSendNotification);
    volumeWeightingLabel.setJustificationType(juce::Justification::centredRight);
    contentComponent.addAndMakeVisible(volumeWeightingLabel);
    
    volumeWeightingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeWeightingSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    contentComponent.addAndMakeVisible(volumeWeightingSlider);
    volumeWeightingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "luxstralVolumeWeightingExp", volumeWeightingSlider);

    softLimitThresholdLabel.setText("Soft Limit Threshold:", juce::dontSendNotification);
    softLimitThresholdLabel.setJustificationType(juce::Justification::centredRight);
    contentComponent.addAndMakeVisible(softLimitThresholdLabel);
    
    softLimitThresholdSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    softLimitThresholdSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    contentComponent.addAndMakeVisible(softLimitThresholdSlider);
    softLimitThresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "luxstralSoftLimitThreshold", softLimitThresholdSlider);

    softLimitKneeLabel.setText("Soft Limit Knee:", juce::dontSendNotification);
    softLimitKneeLabel.setJustificationType(juce::Justification::centredRight);
    contentComponent.addAndMakeVisible(softLimitKneeLabel);
    
    softLimitKneeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    softLimitKneeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    contentComponent.addAndMakeVisible(softLimitKneeSlider);
    softLimitKneeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "luxstralSoftLimitKnee", softLimitKneeSlider);

    // ========================================================================
    // Section: Performance
    // ========================================================================
    performanceSectionLabel.setText("Performance", juce::dontSendNotification);
    performanceSectionLabel.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    performanceSectionLabel.setColour(juce::Label::textColourId, juce::Colours::lightblue);
    contentComponent.addAndMakeVisible(performanceSectionLabel);

    numWorkersLabel.setText("Worker Threads:", juce::dontSendNotification);
    numWorkersLabel.setJustificationType(juce::Justification::centredRight);
    contentComponent.addAndMakeVisible(numWorkersLabel);
    
    numWorkersSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    numWorkersSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    contentComponent.addAndMakeVisible(numWorkersSlider);
    numWorkersAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "luxstralNumWorkers", numWorkersSlider);

    layoutContentComponent();
}

LuxStralSettingsTab::~LuxStralSettingsTab()
{
}

void LuxStralSettingsTab::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    // Section title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(16.0f)).boldened());
    g.drawText("LuxStral Additive Synthesis", getLocalBounds().removeFromTop(30),
               juce::Justification::centred, true);
}

void LuxStralSettingsTab::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(35);  // Skip title
    
    viewport.setBounds(bounds);
    layoutContentComponent();
}

void LuxStralSettingsTab::layoutContentComponent()
{
    const int labelWidth = 140;
    const int sliderWidth = 200;
    const int rowHeight = 30;
    const int sectionSpacing = 15;
    const int itemSpacing = 5;
    const int padding = 20;

    int yPos = padding;
    int contentWidth = viewport.getWidth() - 40;

    // ========================================================================
    // Section: Frequency Range
    // ========================================================================
    freqRangeSectionLabel.setBounds(padding, yPos, contentWidth, 25);
    yPos += 30;
    
    lowFreqLabel.setBounds(padding, yPos, labelWidth, rowHeight);
    lowFreqSlider.setBounds(padding + labelWidth + 10, yPos, sliderWidth, rowHeight);
    yPos += rowHeight + itemSpacing;
    
    highFreqLabel.setBounds(padding, yPos, labelWidth, rowHeight);
    highFreqSlider.setBounds(padding + labelWidth + 10, yPos, sliderWidth, rowHeight);
    yPos += rowHeight + sectionSpacing;

    // ========================================================================
    // Section: Envelope Parameters
    // ========================================================================
    envelopeSectionLabel.setBounds(padding, yPos, contentWidth, 25);
    yPos += 30;
    
    attackLabel.setBounds(padding, yPos, labelWidth, rowHeight);
    attackSlider.setBounds(padding + labelWidth + 10, yPos, sliderWidth, rowHeight);
    yPos += rowHeight + itemSpacing;
    
    releaseLabel.setBounds(padding, yPos, labelWidth, rowHeight);
    releaseSlider.setBounds(padding + labelWidth + 10, yPos, sliderWidth, rowHeight);
    yPos += rowHeight + sectionSpacing;

    // ========================================================================
    // Section: Image Processing
    // ========================================================================
    imageProcSectionLabel.setBounds(padding, yPos, contentWidth, 25);
    yPos += 30;
    
    gammaEnableLabel.setBounds(padding, yPos, labelWidth, rowHeight);
    gammaEnableToggle.setBounds(padding + labelWidth + 10, yPos, 100, rowHeight);
    yPos += rowHeight + itemSpacing;
    
    gammaValueLabel.setBounds(padding, yPos, labelWidth, rowHeight);
    gammaValueSlider.setBounds(padding + labelWidth + 10, yPos, sliderWidth, rowHeight);
    yPos += rowHeight + itemSpacing;
    
    contrastMinLabel.setBounds(padding, yPos, labelWidth, rowHeight);
    contrastMinSlider.setBounds(padding + labelWidth + 10, yPos, sliderWidth, rowHeight);
    yPos += rowHeight + sectionSpacing;

    // ========================================================================
    // Section: Stereo Processing
    // ========================================================================
    stereoSectionLabel.setBounds(padding, yPos, contentWidth, 25);
    yPos += 30;
    
    stereoEnableLabel.setBounds(padding, yPos, labelWidth, rowHeight);
    stereoEnableToggle.setBounds(padding + labelWidth + 10, yPos, 100, rowHeight);
    yPos += rowHeight + itemSpacing;
    
    stereoTempAmpLabel.setBounds(padding, yPos, labelWidth, rowHeight);
    stereoTempAmpSlider.setBounds(padding + labelWidth + 10, yPos, sliderWidth, rowHeight);
    yPos += rowHeight + sectionSpacing;

    // ========================================================================
    // Section: Dynamics Processing
    // ========================================================================
    dynamicsSectionLabel.setBounds(padding, yPos, contentWidth, 25);
    yPos += 30;
    
    volumeWeightingLabel.setBounds(padding, yPos, labelWidth, rowHeight);
    volumeWeightingSlider.setBounds(padding + labelWidth + 10, yPos, sliderWidth, rowHeight);
    yPos += rowHeight + itemSpacing;
    
    softLimitThresholdLabel.setBounds(padding, yPos, labelWidth, rowHeight);
    softLimitThresholdSlider.setBounds(padding + labelWidth + 10, yPos, sliderWidth, rowHeight);
    yPos += rowHeight + itemSpacing;
    
    softLimitKneeLabel.setBounds(padding, yPos, labelWidth, rowHeight);
    softLimitKneeSlider.setBounds(padding + labelWidth + 10, yPos, sliderWidth, rowHeight);
    yPos += rowHeight + sectionSpacing;

    // ========================================================================
    // Section: Performance
    // ========================================================================
    performanceSectionLabel.setBounds(padding, yPos, contentWidth, 25);
    yPos += 30;
    
    numWorkersLabel.setBounds(padding, yPos, labelWidth, rowHeight);
    numWorkersSlider.setBounds(padding + labelWidth + 10, yPos, sliderWidth, rowHeight);
    yPos += rowHeight + padding;

    // Set content component size for scrolling
    contentComponent.setSize(viewport.getWidth(), yPos);
}
