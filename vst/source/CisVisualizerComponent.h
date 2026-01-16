#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <cstdint>

// Forward declaration
class Sp3ctraAudioProcessor;

/**
 * @brief CIS Visualizer Component
 * 
 * Displays CIS sensor data (RGB channels) with horizontal layout
 * and vertical wave effect using tan() function (matching firmware behavior).
 * 
 * Features:
 * - 30 FPS refresh rate (33ms Timer)
 * - Lock-free data reading from AudioImageBuffers
 * - RGB overlay rendering with transparency
 * - Linear interpolation for display scaling (3456 or 1728 → 360 pixels)
 * - Thread-safe: reads in Timer callback, renders in paint()
 */
class CisVisualizerComponent : public juce::Component, 
                               private juce::Timer
{
public:
    CisVisualizerComponent(Sp3ctraAudioProcessor& proc);
    ~CisVisualizerComponent() override;
    
    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;
    
private:
    // Timer callback (30 FPS)
    void timerCallback() override;
    
    // Update local CIS data from AudioImageBuffers (lock-free)
    void updateCisData();
    
    // Interpolate CIS pixel value for display position
    uint8_t interpolateCisPixel(const uint8_t* buffer, int displayX, 
                               int displayWidth);
    
    // Draw vertical wave column for one channel
    void drawWaveColumn(juce::Graphics& g, int x, int centerY, 
                       float angle, juce::Colour colour, float alpha);
    
    // Local data buffers (copied from AudioImageBuffers)
    std::vector<uint8_t> localDataR;
    std::vector<uint8_t> localDataG;
    std::vector<uint8_t> localDataB;
    int cisPixelsCount = 0;
    
    // Configuration
    static constexpr int TIMER_FPS = 30;           // 30 Hz refresh
    static constexpr float ALPHA_RGB = 0.6f;       // RGB transparency
    
    // Reference to processor
    Sp3ctraAudioProcessor& processor;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CisVisualizerComponent)
};
