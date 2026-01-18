#include "CisVisualizerComponent.h"
#include "PluginProcessor.h"
#include <cmath>
#include <algorithm>
#include <cstring>

// Include C core headers for CIS data access
extern "C" {
    #include "../../src/audio/buffers/audio_image_buffers.h"
    #include "../../src/config/config_instrument.h"
    #include "../../src/config/config_loader.h"
}

//==============================================================================
CisVisualizerComponent::CisVisualizerComponent(Sp3ctraAudioProcessor& proc)
    : processor(proc)
{
    // Start timer at 30 FPS (33ms interval)
    startTimer(1000 / TIMER_FPS);
}

CisVisualizerComponent::~CisVisualizerComponent()
{
    stopTimer();
}

//==============================================================================
void CisVisualizerComponent::paint(juce::Graphics& g)
{
    // 🛡️ CRITICAL: Early return if suspended (prevents CoreGraphics crash during prepareToPlay)
    if (isSuspended.load()) {
        g.fillAll(juce::Colour(0xff1a1a1a));
        return;
    }
    
    auto bounds = getLocalBounds();
    int displayWidth = bounds.getWidth();
    int displayHeight = bounds.getHeight();
    int centerY = displayHeight / 2;
    
    // Check if we have CIS data
    if (cisPixelsCount == 0) {
        g.fillAll(juce::Colour(0xff1a1a1a));
        g.setColour(juce::Colours::grey);
        g.drawText("Waiting for CIS data...", bounds, 
                   juce::Justification::centred);
        return;
    }
    
    // Get current visualizer mode
    auto& apvts = processor.getAPVTS();
    int visualizerMode = (int)apvts.getRawParameterValue("visualizerMode")->load();
    
    if (visualizerMode == 0) {
        // MODE 0: IMAGE - Direct RGB vertical display with dark background
        g.fillAll(juce::Colour(0xff1a1a1a));
        // MODE 1: IMAGE - Direct RGB display
        for (int x = 0; x < displayWidth; x++) {
            // Interpolate CIS values for this display position
            uint8_t rVal = interpolateCisPixel(localDataR.data(), x, displayWidth);
            uint8_t gVal = interpolateCisPixel(localDataG.data(), x, displayWidth);
            uint8_t bVal = interpolateCisPixel(localDataB.data(), x, displayWidth);
            
            // Create RGB color directly from sensor values
            // 255,255,255 = white, 0,0,0 = black, 255,0,0 = red, etc.
            juce::Colour pixelColour(rVal, gVal, bVal);
            
            // Draw vertical line for this pixel
            g.setColour(pixelColour);
            g.fillRect(x, 0, 1, displayHeight);
        }
    } else if (visualizerMode == 1) {
        // MODE 1: WAVEFORM - Bargraph with RGB color and height based on luminosity
        // Start with white background
        g.fillAll(juce::Colours::white);
        
        // Draw center line for reference
        g.setColour(juce::Colour(0xffcccccc));
        g.drawHorizontalLine(centerY, 0.0f, static_cast<float>(displayWidth));
        
        for (int x = 0; x < displayWidth; x++) {
            // Interpolate CIS values for this display position (SAME AS IMAGE MODE)
            uint8_t rVal = interpolateCisPixel(localDataR.data(), x, displayWidth);
            uint8_t gVal = interpolateCisPixel(localDataG.data(), x, displayWidth);
            uint8_t bVal = interpolateCisPixel(localDataB.data(), x, displayWidth);
            
            // Use RGB color directly (same as Image mode - it works!)
            juce::Colour pixelColour(rVal, gVal, bVal);
            
            // Calculate luminosity (brightness) = max of R, G, B
            int maxChannel = std::max({rVal, gVal, bVal});
            
            // Calculate bar height based on luminosity (0-255 → 0-halfHeight)
            int halfHeight = displayHeight / 2;
            int barHeight = (maxChannel * halfHeight) / 255;
            
            // Draw bar with RGB color and height proportional to luminosity
            if (barHeight > 0) {
                g.setColour(pixelColour);
                g.fillRect(x, centerY - barHeight, 1, barHeight * 2);
            }
        }
    } else {
        // MODE 3: WAVEFORM INVERSÉ - Bar height inversely proportional to luminosity
        // Low luminosity = tall bar, high luminosity = short bar
        // Start with white background
        g.fillAll(juce::Colours::white);
        
        for (int x = 0; x < displayWidth; x++) {
            // Interpolate CIS values for this display position
            uint8_t rVal = interpolateCisPixel(localDataR.data(), x, displayWidth);
            uint8_t gVal = interpolateCisPixel(localDataG.data(), x, displayWidth);
            uint8_t bVal = interpolateCisPixel(localDataB.data(), x, displayWidth);
            
            // Use RGB color directly
            juce::Colour pixelColour(rVal, gVal, bVal);
            
            // Calculate luminosity (brightness) = max of R, G, B
            int maxChannel = std::max({rVal, gVal, bVal});
            
            // INVERSE: Calculate bar height inversely proportional to luminosity
            // 255 (bright) → 0 (no bar), 0 (dark) → halfHeight (full bar)
            int halfHeight = displayHeight / 2;
            int barHeight = ((255 - maxChannel) * halfHeight) / 255;
            
            // Draw inverted bar with RGB color
            if (barHeight > 0) {
                g.setColour(pixelColour);
                g.fillRect(x, centerY - barHeight, 1, barHeight * 2);
            }
        }
    }
}

void CisVisualizerComponent::resized()
{
    // Nothing to resize - we draw directly in paint()
}

//==============================================================================
void CisVisualizerComponent::timerCallback()
{
    updateCisData();
    repaint();
}

//==============================================================================
void CisVisualizerComponent::suspend()
{
    isSuspended.store(true);  // Block paint() immediately (returns early with black screen)
    stopTimer();
    // NOTE: setVisible(false) causes CALayer dealloc crash - just use atomic flag!
}

void CisVisualizerComponent::resume()
{
    isSuspended.store(false);  // Allow paint() to resume
    startTimer(1000 / TIMER_FPS);
}

void CisVisualizerComponent::updateCisData()
{
    // Get Sp3ctraCore from processor
    auto* core = processor.getSp3ctraCore();
    if (!core || !core->isInitialized()) {
        cisPixelsCount = 0;
        return;
    }
    
    // Get AudioImageBuffers
    auto* buffers = core->getAudioImageBuffers();
    if (!buffers || !buffers->initialized) {
        cisPixelsCount = 0;
        return;
    }
    
    // Get read pointers (lock-free, thread-safe)
    uint8_t *pR, *pG, *pB;
    audio_image_buffers_get_read_pointers(buffers, &pR, &pG, &pB);
    
    // Determine CIS pixel count based on DPI configuration
    extern sp3ctra_config_t g_sp3ctra_config;
    cisPixelsCount = (g_sp3ctra_config.sensor_dpi == 400) 
                     ? CIS_400DPI_PIXELS_NB 
                     : CIS_200DPI_PIXELS_NB;
    
    // Resize local buffers if needed
    if (localDataR.size() != static_cast<size_t>(cisPixelsCount)) {
        localDataR.resize(cisPixelsCount);
        localDataG.resize(cisPixelsCount);
        localDataB.resize(cisPixelsCount);
    }
    
    // Copy CIS data to local buffers
    std::memcpy(localDataR.data(), pR, cisPixelsCount);
    std::memcpy(localDataG.data(), pG, cisPixelsCount);
    std::memcpy(localDataB.data(), pB, cisPixelsCount);
}

//==============================================================================
uint8_t CisVisualizerComponent::interpolateCisPixel(
    const uint8_t* buffer, int displayX, int displayWidth)
{
    if (cisPixelsCount == 0 || displayWidth == 0) {
        return 0;
    }
    
    // Map display position to CIS buffer position
    float cisPosition = static_cast<float>(displayX) * (cisPixelsCount - 1) 
                       / static_cast<float>(displayWidth - 1);
    int cisIndex = static_cast<int>(cisPosition);
    float fraction = cisPosition - cisIndex;
    
    // Linear interpolation between two adjacent CIS pixels
    if (cisIndex + 1 < cisPixelsCount) {
        float val = buffer[cisIndex] * (1.0f - fraction) 
                   + buffer[cisIndex + 1] * fraction;
        return static_cast<uint8_t>(val);
    }
    
    return buffer[cisIndex];
}

void CisVisualizerComponent::drawWaveColumn(
    juce::Graphics& g, int x, int centerY, float angle, 
    juce::Colour baseColour, float alpha)
{
    int halfHeight = getHeight() / 2;
    
    // Draw symmetric wave effect above and below center line
    for (int y = 0; y < halfHeight; y++) {
        // Calculate intensity using tan() function (matches firmware behavior)
        float pixelIntensity;
        if (angle < M_PI / 2.0f) {
            pixelIntensity = std::tan(angle) * (y + 1) / 15.0f;
        } else {
            pixelIntensity = 1.0f;  // Saturation at maximum angle
        }
        
        // Clamp intensity to [0, 1]
        pixelIntensity = std::clamp(pixelIntensity, 0.0f, 1.0f);
        
        // Apply both alpha and intensity
        juce::Colour pixelColour = baseColour.withAlpha(alpha * pixelIntensity);
        
        g.setColour(pixelColour);
        
        // Draw pixel above center line
        g.fillRect(x, centerY - y - 1, 1, 1);
        
        // Draw pixel below center line
        g.fillRect(x, centerY + y, 1, 1);
    }
}
