#include "PluginProcessor.h"
#include "PluginEditor.h"

// Include C headers for global config access
extern "C" {
    #include "../../src/core/context.h"
    #include "../../src/utils/logger.h"
}

//==============================================================================
// Create parameter layout (called once during construction)
juce::AudioProcessorValueTreeState::ParameterLayout Sp3ctraAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    // UDP Port (1024 - 65535)
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        PARAM_UDP_PORT,
        "UDP Port",
        1024, 65535,
        Sp3ctraConstants::DEFAULT_UDP_PORT
    ));
    
    // UDP Address - 4 separate bytes (0-255 each)
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        PARAM_UDP_BYTE1, "UDP Byte 1", 0, 255, 192));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        PARAM_UDP_BYTE2, "UDP Byte 2", 0, 255, 168));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        PARAM_UDP_BYTE3, "UDP Byte 3", 0, 255, 100));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        PARAM_UDP_BYTE4, "UDP Byte 4", 0, 255, 10));
    
    // Sensor DPI (200 or 400)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        PARAM_SENSOR_DPI,
        "Sensor DPI",
        juce::StringArray{"200 DPI", "400 DPI"},
        1  // Default = 400 DPI
    ));
    
    // Log Level
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        PARAM_LOG_LEVEL,
        "Log Level",
        juce::StringArray{"Error", "Warning", "Info", "Debug"},
        Sp3ctraConstants::DEFAULT_LOG_LEVEL  // Default = Info (2)
    ));
    
    // Visualizer Mode (0 = Image, 1 = Waveform, 2 = Inverted Waveform)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        PARAM_VISUALIZER_MODE,
        "Visualizer Mode",
        juce::StringArray{"Image", "Waveform", "Inverted Waveform"},
        0  // Default = Image mode
    ));
    
    return { params.begin(), params.end() };
}

//==============================================================================
Sp3ctraAudioProcessor::Sp3ctraAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
       apvts(*this, nullptr, "Parameters", createParameterLayout())
#else
     : apvts(*this, nullptr, "Parameters", createParameterLayout())
#endif
{
    juce::Logger::writeToLog("=============================================================");
    juce::Logger::writeToLog("Sp3ctraAudioProcessor: Constructor - Initializing VST plugin");
    juce::Logger::writeToLog("  Using APVTS (AudioProcessorValueTreeState) for parameters");
    juce::Logger::writeToLog("=============================================================");
    
    // Cache parameter pointers for fast access
    udpPortParam = apvts.getRawParameterValue(PARAM_UDP_PORT);
    udpByte1Param = apvts.getRawParameterValue(PARAM_UDP_BYTE1);
    udpByte2Param = apvts.getRawParameterValue(PARAM_UDP_BYTE2);
    udpByte3Param = apvts.getRawParameterValue(PARAM_UDP_BYTE3);
    udpByte4Param = apvts.getRawParameterValue(PARAM_UDP_BYTE4);
    sensorDpiParam = apvts.getRawParameterValue(PARAM_SENSOR_DPI);
    logLevelParam = apvts.getRawParameterValue(PARAM_LOG_LEVEL);
    visualizerModeParam = apvts.getRawParameterValue(PARAM_VISUALIZER_MODE);
    
    // Register as listener for parameter changes
    apvts.addParameterListener(PARAM_UDP_PORT, this);
    apvts.addParameterListener(PARAM_UDP_BYTE1, this);
    apvts.addParameterListener(PARAM_UDP_BYTE2, this);
    apvts.addParameterListener(PARAM_UDP_BYTE3, this);
    apvts.addParameterListener(PARAM_UDP_BYTE4, this);
    apvts.addParameterListener(PARAM_SENSOR_DPI, this);
    apvts.addParameterListener(PARAM_LOG_LEVEL, this);
    
    // Create Sp3ctra core
    sp3ctraCore = std::make_unique<Sp3ctraCore>();
    
    // NO .ini loading! Parameters come from APVTS (saved in DAW project)
    // Initialize configuration from APVTS parameters
    applyConfigurationToCore();
    
    // Start UDP receiver thread
    udpThread = std::make_unique<UdpReceiverThread>(sp3ctraCore.get());
    udpThread->startThread();
    
    juce::Logger::writeToLog("=============================================================");
    juce::Logger::writeToLog("Sp3ctraAudioProcessor: Initialization COMPLETE ✓");
    juce::Logger::writeToLog(juce::String::formatted("  - UDP listening on %s:%d",
        getUdpAddressString().toRawUTF8(),
        (int)udpPortParam->load()));
    juce::Logger::writeToLog("  - Ready to receive IMAGE_DATA and IMU packets");
    juce::Logger::writeToLog("  - Parameters managed by APVTS (saved in DAW project)");
    juce::Logger::writeToLog("=============================================================");
}

Sp3ctraAudioProcessor::~Sp3ctraAudioProcessor()
{
    juce::Logger::writeToLog("=============================================================");
    juce::Logger::writeToLog("Sp3ctraAudioProcessor: Destructor - Shutting down");
    juce::Logger::writeToLog("=============================================================");
    
    // Stop UDP thread first (blocks until thread exits)
    if (udpThread) {
        juce::Logger::writeToLog("Sp3ctraAudioProcessor: Stopping UDP thread...");
        udpThread->requestStop();
        udpThread->stopThread(2000);  // 2 second timeout
        udpThread.reset();
        juce::Logger::writeToLog("Sp3ctraAudioProcessor: UDP thread stopped");
    }
    
    // Cleanup core (closes socket, frees buffers)
    if (sp3ctraCore) {
        juce::Logger::writeToLog("Sp3ctraAudioProcessor: Shutting down core...");
        sp3ctraCore->shutdown();
        sp3ctraCore.reset();
        juce::Logger::writeToLog("Sp3ctraAudioProcessor: Core shutdown complete");
    }
    
    juce::Logger::writeToLog("=============================================================");
    juce::Logger::writeToLog("Sp3ctraAudioProcessor: Destructor complete");
    juce::Logger::writeToLog("=============================================================");
}

//==============================================================================
const juce::String Sp3ctraAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool Sp3ctraAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool Sp3ctraAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool Sp3ctraAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double Sp3ctraAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int Sp3ctraAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int Sp3ctraAudioProcessor::getCurrentProgram()
{
    return 0;
}

void Sp3ctraAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String Sp3ctraAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void Sp3ctraAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void Sp3ctraAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    juce::ignoreUnused (sampleRate, samplesPerBlock);
}

void Sp3ctraAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool Sp3ctraAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void Sp3ctraAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    
    // Generate 440Hz test tone at 10% volume
    const float frequency = 440.0f;
    const float sampleRate = (float)getSampleRate();
    const float phaseIncrement = 2.0f * juce::MathConstants<float>::pi * frequency / sampleRate;
    const float volume = 0.1f;
    
    // CRITICAL FIX: Use member variable for phase persistence (not static)
    // This avoids glitches and ensures proper state management
    const int numSamples = buffer.getNumSamples();
    
    // Generate samples
    for (int sample = 0; sample < numSamples; ++sample)
    {
        float currentSample = std::sin(testTonePhase) * volume;
        
        // Write to all output channels (mono->stereo duplication)
        for (int channel = 0; channel < totalNumOutputChannels; ++channel)
        {
            buffer.setSample(channel, sample, currentSample);
        }
        
        // Advance phase
        testTonePhase += phaseIncrement;
        
        // Wrap phase to avoid precision issues
        while (testTonePhase >= 2.0f * juce::MathConstants<float>::pi)
            testTonePhase -= 2.0f * juce::MathConstants<float>::pi;
    }
    
    juce::ignoreUnused(midiMessages);
}

//==============================================================================
bool Sp3ctraAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* Sp3ctraAudioProcessor::createEditor()
{
    return new Sp3ctraAudioProcessorEditor (*this);
}

//==============================================================================
// APVTS State Management (automatic save/restore in DAW projects)
void Sp3ctraAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // APVTS handles serialization automatically via ValueTree
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
    
    juce::Logger::writeToLog("Sp3ctraAudioProcessor: State saved to DAW project");
}

void Sp3ctraAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // APVTS handles deserialization automatically via ValueTree
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    
    if (xmlState.get() != nullptr) {
        if (xmlState->hasTagName(apvts.state.getType())) {
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
            juce::Logger::writeToLog("Sp3ctraAudioProcessor: State restored from settings");
            
            // Stop UDP thread
            if (udpThread) {
                juce::Logger::writeToLog("Sp3ctraAudioProcessor: Restarting UDP with restored settings...");
                udpThread->requestStop();
                udpThread->stopThread(2000);
                udpThread.reset();
            }
            
            // Re-apply configuration to core with restored parameters
            applyConfigurationToCore();
            
            // Restart UDP thread with new configuration
            udpThread = std::make_unique<UdpReceiverThread>(sp3ctraCore.get());
            udpThread->startThread();
            
            juce::Logger::writeToLog(juce::String::formatted(
                "Sp3ctraAudioProcessor: UDP restarted with %s:%d",
                getUdpAddressString().toRawUTF8(),
                (int)udpPortParam->load()));
        }
    }
}

//==============================================================================
// Helper to build UDP address string from 4 bytes
juce::String Sp3ctraAudioProcessor::getUdpAddressString() const
{
    int b1 = (int)udpByte1Param->load();
    int b2 = (int)udpByte2Param->load();
    int b3 = (int)udpByte3Param->load();
    int b4 = (int)udpByte4Param->load();
    
    return juce::String::formatted("%d.%d.%d.%d", b1, b2, b3, b4);
}

//==============================================================================
// Parameter Change Listener (called when user modifies parameters in UI)
void Sp3ctraAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    juce::Logger::writeToLog(juce::String::formatted(
        "Sp3ctraAudioProcessor: Parameter '%s' changed to %.2f",
        parameterID.toRawUTF8(), newValue
    ));
    
    // Check if UDP parameters changed (need to restart thread)
    bool needsUdpRestart = (parameterID == PARAM_UDP_PORT || 
                           parameterID == PARAM_UDP_BYTE1 ||
                           parameterID == PARAM_UDP_BYTE2 ||
                           parameterID == PARAM_UDP_BYTE3 ||
                           parameterID == PARAM_UDP_BYTE4);
    
    if (needsUdpRestart) {
        juce::Logger::writeToLog("Sp3ctraAudioProcessor: UDP parameter changed - restarting thread...");
        
        // Stop UDP thread
        if (udpThread) {
            udpThread->requestStop();
            udpThread->stopThread(2000);
            udpThread.reset();
        }
        
        // Apply new configuration to core (this restarts the socket)
        applyConfigurationToCore();
        
        // Restart UDP thread with new socket
        udpThread = std::make_unique<UdpReceiverThread>(sp3ctraCore.get());
        udpThread->startThread();
        
        juce::Logger::writeToLog("Sp3ctraAudioProcessor: UDP thread restarted successfully");
    } else {
        // For non-UDP parameters, just apply config
        applyConfigurationToCore();
    }
}

//==============================================================================
// Apply APVTS parameters to Sp3ctraCore and global C config
void Sp3ctraAudioProcessor::applyConfigurationToCore()
{
    if (!sp3ctraCore) {
        return;  // Core not initialized yet
    }
    
    // Read current APVTS parameters
    int udpPort = (int)udpPortParam->load();
    int dpiChoice = (int)sensorDpiParam->load();  // 0=200, 1=400
    int logLevel = (int)logLevelParam->load();
    
    // Map DPI choice to actual DPI value
    int sensorDpi = (dpiChoice == 0) ? 200 : 400;
    
    // Build UDP address from 4 bytes
    juce::String udpAddress = getUdpAddressString();
    
    // Update global C config (used by udpThread)
    extern sp3ctra_config_t g_sp3ctra_config;
    g_sp3ctra_config.udp_port = udpPort;
    strncpy(g_sp3ctra_config.udp_address, udpAddress.toRawUTF8(), 
            sizeof(g_sp3ctra_config.udp_address) - 1);
    g_sp3ctra_config.udp_address[sizeof(g_sp3ctra_config.udp_address) - 1] = '\0';
    g_sp3ctra_config.sensor_dpi = sensorDpi;
    g_sp3ctra_config.log_level = (log_level_t)logLevel;
    
    // Update logger level immediately
    logger_init((log_level_t)logLevel);
    
    // Create ActiveConfig for Sp3ctraCore
    Sp3ctraCore::ActiveConfig config;
    config.udpPort = udpPort;
    config.udpAddress = udpAddress.toStdString();
    config.multicastInterface = "";  // Auto-detect
    config.logLevel = logLevel;
    
    // Apply to core (this will restart UDP socket if needed)
    if (!sp3ctraCore->initialize(config)) {
        juce::Logger::writeToLog("Sp3ctraAudioProcessor: WARNING - Failed to apply configuration");
    } else {
        juce::Logger::writeToLog(juce::String::formatted(
            "Sp3ctraAudioProcessor: Configuration applied - %s:%d, %d DPI, log level %d",
            udpAddress.toRawUTF8(), udpPort, sensorDpi, logLevel
        ));
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Sp3ctraAudioProcessor();
}
