/*
 * three_band_eq.h
 *
 * Created on: 16 May 2025
 * Author: CISYNTH Team
 */

#ifndef THREE_BAND_EQ_H
#define THREE_BAND_EQ_H

#include "pareq.h"

class ThreeBandEQ {
public:
  ThreeBandEQ();
  ~ThreeBandEQ();

  // Initialize the equalizer with sample rate
  void initialize(float sampleRate);

  // Process audio data
  void process(int numSamples, int numChannels, float *data[]);

  // Enable/disable the equalizer
  void setEnabled(bool enabled);
  bool isEnabled() const { return enabled; }

  // Control parameters
  void setLowGain(float gain);      // gain in dB
  void setMidGain(float gain);      // gain in dB
  void setHighGain(float gain);     // gain in dB
  void setMidFrequency(float freq); // frequency in Hz

  // Get current parameter values
  float getLowGain() const { return lowGain; }
  float getMidGain() const { return midGain; }
  float getHighGain() const { return highGain; }
  float getMidFrequency() const { return midFreq; }

  // Default frequency values
  static constexpr float DEFAULT_LOW_FREQ = 250.0f; // Low/Mid crossover
  static constexpr float DEFAULT_MID_FREQ =
      1000.0f; // Default mid center frequency
  static constexpr float DEFAULT_HIGH_FREQ = 4000.0f; // Mid/High crossover

private:
  // Three parametric equalizers for the three bands
  Pareq lowEQ;  // Low frequency band
  Pareq midEQ;  // Mid frequency band
  Pareq highEQ; // High frequency band

  // Current parameter values
  float lowGain;  // in dB
  float midGain;  // in dB
  float highGain; // in dB
  float midFreq;  // in Hz

  // Temporary buffer for multi-stage processing
  float **tempBuffer;
  int tempBufferSize;

  // State
  bool enabled;
  float sampleRate;

  // Allocate and free temporary buffer
  void allocateTempBuffer(int numSamples, int numChannels);
  void freeTempBuffer();
};

// Global instance for C API compatibility
extern ThreeBandEQ *gEqualizer;

// C API functions
extern "C" {
void eq_Init(float sampleRate);
void eq_Cleanup();
void eq_Process(int numSamples, int numChannels, float *data[]);
void eq_Enable(int enabled);
int eq_IsEnabled();
void eq_SetLowGain(float gain);
void eq_SetMidGain(float gain);
void eq_SetHighGain(float gain);
void eq_SetMidFrequency(float freq);
}

#endif /* THREE_BAND_EQ_H */
