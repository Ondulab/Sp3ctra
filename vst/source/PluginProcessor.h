#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Sp3ctraCore.h"
#include "UdpReceiverThread.h"
#include "AudioProcessingThread.h"  // Thread for synth_AudioProcess()
#include "Sp3ctraConstants.h"

//==============================================================================
/**
 * @brief Sp3ctra VST Audio Processor
 * 
 * Main VST plugin class that integrates:
 * - UDP reception thread (IMAGE_DATA + IMU packets)
 * - Core synthesis engine (Sp3ctraCore)
 * - Audio processing (processBlock)
 * - VST parameters (APVTS with UDP config, sensor DPI, log level)
 */
class Sp3ctraAudioProcessor  : public juce::AudioProcessor,
                                public juce::AudioProcessorValueTreeState::Listener
{
public:
    //==============================================================================
    Sp3ctraAudioProcessor();
    ~Sp3ctraAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    //==============================================================================
    // AudioProcessorValueTreeState::Listener interface
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    
    //==============================================================================
    // Public accessors for UI
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    Sp3ctraCore* getSp3ctraCore() { return sp3ctraCore.get(); }
    
    // Helper to build UDP address string from 4 bytes
    juce::String getUdpAddressString() const;

private:
    //==============================================================================
    // Helper to create parameter layout
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    // Apply updated configuration to Sp3ctraCore
    // needsSocketRestart: true = full reinit (UDP change), false = just update g_sp3ctra_config
    void applyConfigurationToCore(bool needsSocketRestart = true);
    
    //==============================================================================
    // LuxStral synthesis engine state
    bool luxstralInitialized = false;
    
    // Test tone phase accumulator (fallback if LuxStral not working)
    // Note: testTonePhase removed - no longer using 440Hz fallback tone
    
    // ✨ Sp3ctra Core Integration
    std::unique_ptr<Sp3ctraCore> sp3ctraCore;
    std::unique_ptr<UdpReceiverThread> udpThread;
    std::unique_ptr<AudioProcessingThread> audioProcessingThread;  // Calls synth_AudioProcess() in loop
    
    // ✨ VST Parameters via AudioProcessorValueTreeState
    juce::AudioProcessorValueTreeState apvts;
    
    // Parameter IDs (for consistency)
    static constexpr const char* PARAM_UDP_PORT = "udpPort";
    static constexpr const char* PARAM_UDP_BYTE1 = "udpByte1";
    static constexpr const char* PARAM_UDP_BYTE2 = "udpByte2";
    static constexpr const char* PARAM_UDP_BYTE3 = "udpByte3";
    static constexpr const char* PARAM_UDP_BYTE4 = "udpByte4";
    static constexpr const char* PARAM_SENSOR_DPI = "sensorDpi";
    static constexpr const char* PARAM_LOG_LEVEL = "logLevel";
    static constexpr const char* PARAM_VISUALIZER_MODE = "visualizerMode";
    
    // Quick access to parameters (cached, no atomic overhead)
    std::atomic<float>* udpPortParam = nullptr;
    std::atomic<float>* udpByte1Param = nullptr;
    std::atomic<float>* udpByte2Param = nullptr;
    std::atomic<float>* udpByte3Param = nullptr;
    std::atomic<float>* udpByte4Param = nullptr;
    std::atomic<float>* sensorDpiParam = nullptr;
    std::atomic<float>* logLevelParam = nullptr;
    std::atomic<float>* visualizerModeParam = nullptr;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sp3ctraAudioProcessor)
};
