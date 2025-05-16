/*
 * three_band_eq.cpp
 *
 * Created on: 16 May 2025
 * Author: CISYNTH Team
 */

#include "three_band_eq.h"
#include "config.h"
#include <cstdio>
#include <cstring>
#include <iostream>

// Global instance
ThreeBandEQ *gEqualizer = nullptr;

ThreeBandEQ::ThreeBandEQ()
    : lowGain(0.0f), midGain(0.0f), highGain(0.0f), midFreq(DEFAULT_MID_FREQ),
      tempBuffer(nullptr), tempBufferSize(0), enabled(false),
      sampleRate(SAMPLING_FREQUENCY) {}

ThreeBandEQ::~ThreeBandEQ() { freeTempBuffer(); }

void ThreeBandEQ::initialize(float sampleRate) {
  this->sampleRate = sampleRate;

  // Initialize each equalizer band
  lowEQ.setfsamp(sampleRate);
  midEQ.setfsamp(sampleRate);
  highEQ.setfsamp(sampleRate);

  // Set default parameters
  setLowGain(0.0f);
  setMidGain(0.0f);
  setHighGain(0.0f);
  setMidFrequency(DEFAULT_MID_FREQ);

  // Reset the equalizers
  lowEQ.reset();
  midEQ.reset();
  highEQ.reset();

  std::cout << "Three-band equalizer initialized with sample rate: "
            << sampleRate << " Hz" << std::endl;
}

void ThreeBandEQ::allocateTempBuffer(int numSamples, int numChannels) {
  // Only allocate if needed
  if (tempBuffer == nullptr || tempBufferSize < numSamples) {
    freeTempBuffer();

    tempBufferSize = numSamples;
    tempBuffer = new float *[numChannels];

    for (int i = 0; i < numChannels; i++) {
      tempBuffer[i] = new float[numSamples];
    }
  }
}

void ThreeBandEQ::freeTempBuffer() {
  if (tempBuffer != nullptr) {
    for (int i = 0; i < tempBufferSize; i++) {
      delete[] tempBuffer[i];
    }
    delete[] tempBuffer;
    tempBuffer = nullptr;
    tempBufferSize = 0;
  }
}

void ThreeBandEQ::setEnabled(bool enabled) {
  this->enabled = enabled;
  std::cout << "Three-band equalizer " << (enabled ? "enabled" : "disabled")
            << std::endl;
}

void ThreeBandEQ::setLowGain(float gain) {
  lowGain = gain;
  // Configure the low EQ as a low-shelf filter
  lowEQ.setparam(DEFAULT_LOW_FREQ, gain);
  std::cout << "EQ Low gain set to " << gain << " dB" << std::endl;
}

void ThreeBandEQ::setMidGain(float gain) {
  midGain = gain;
  // Configure the mid EQ as a peaking filter
  midEQ.setparam(midFreq, gain);
  std::cout << "EQ Mid gain set to " << gain << " dB" << std::endl;
}

void ThreeBandEQ::setHighGain(float gain) {
  highGain = gain;
  // Configure the high EQ as a high-shelf filter
  highEQ.setparam(DEFAULT_HIGH_FREQ, gain);
  std::cout << "EQ High gain set to " << gain << " dB" << std::endl;
}

void ThreeBandEQ::setMidFrequency(float freq) {
  midFreq = freq;
  // Update the mid EQ with the new frequency
  midEQ.setparam(freq, midGain);
  std::cout << "EQ Mid frequency set to " << freq << " Hz" << std::endl;
}

void ThreeBandEQ::process(int numSamples, int numChannels, float *data[]) {
  if (!enabled || numSamples <= 0 || numChannels <= 0) {
    return;
  }

  // Make sure we don't exceed the max channel count supported by Pareq
  int processChannels = (numChannels > 4) ? 4 : numChannels;

  // Prepare the equalizers to process this block
  lowEQ.prepare(numSamples);
  midEQ.prepare(numSamples);
  highEQ.prepare(numSamples);

  // Process through each band
  lowEQ.process(numSamples, processChannels, data);
  midEQ.process(numSamples, processChannels, data);
  highEQ.process(numSamples, processChannels, data);
}

// C API functions
extern "C" {

void eq_Init(float sampleRate) {
  if (!gEqualizer) {
    gEqualizer = new ThreeBandEQ();
    if (gEqualizer) {
      gEqualizer->initialize(sampleRate);
    }
  }
}

void eq_Cleanup() {
  if (gEqualizer) {
    delete gEqualizer;
    gEqualizer = nullptr;
  }
}

void eq_Process(int numSamples, int numChannels, float *data[]) {
  if (gEqualizer) {
    gEqualizer->process(numSamples, numChannels, data);
  }
}

void eq_Enable(int enabled) {
  if (gEqualizer) {
    gEqualizer->setEnabled(enabled != 0);
  }
}

int eq_IsEnabled() { return (gEqualizer && gEqualizer->isEnabled()) ? 1 : 0; }

void eq_SetLowGain(float gain) {
  if (gEqualizer) {
    gEqualizer->setLowGain(gain);
  }
}

void eq_SetMidGain(float gain) {
  if (gEqualizer) {
    gEqualizer->setMidGain(gain);
  }
}

void eq_SetHighGain(float gain) {
  if (gEqualizer) {
    gEqualizer->setHighGain(gain);
  }
}

void eq_SetMidFrequency(float freq) {
  if (gEqualizer) {
    gEqualizer->setMidFrequency(freq);
  }
}

} // extern "C"
