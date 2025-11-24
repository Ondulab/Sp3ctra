// ZitaRev1.cpp
// --------
// Open source implementation of Fons Adriaensen's zita-rev1 reverb
// Algorithm adapted for Sp3ctra
//
// Original algorithm by Fons Adriaensen <fons@linuxaudio.org>
// C++ implementation based on PelleJuul's version
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

#include "ZitaRev1.h"
#include "config.h"
#include "../utils/logger.h"

// Constantes pour les délais (nombres premiers multipliés par 8 pour des délais longs)
// Original: 743, 809, 877, 947, 1019, 1097, 1171, 1259 (~16-28ms)
// Now: ~130-230ms for true long reverb tails
static const int ZITA_PRIME_DELAYS[8] = {5944, 6472, 7016, 7576,
                                         8152, 8776, 9368, 10072};

ZitaRev1::ZitaRev1() {
  // Initialisation des paramètres
  for (int i = 0; i < NUM_PARAMS; i++)
    _parameters[i] = 0.0f;

  // Valeurs par défaut
  _parameters[ROOMSIZE] = 0.7f;
  _parameters[DAMPING] = 0.5f;
  _parameters[WIDTH] = 1.0f;
  _parameters[PREDELAY] = 0.02f;
  _parameters[MIX] = 0.5f;

  // Initialisation des buffers
  memset(_delayLines, 0, sizeof(float) * NUM_DELAY_LINES * MAX_DELAY_SIZE);
  memset(_preDelayBuffer, 0, sizeof(float) * MAX_PREDELAY_SIZE);

  // Autres paramètres
  _sampleRate = 44100.0f;
  _gain0 = 1.0f;                    // Gain interne de la réverbération (deprecated)
  _gain1 = 1.0f - _parameters[MIX]; // Dry gain
  _gain2 = _parameters[MIX];        // Wet gain
  
  // Initialize smoothing parameters for click-free parameter changes
  // Using very aggressive smoothing: 0.0002 gives ~300ms transition time
  _currentGain0 = 1.0f;
  _targetGain0 = 1.0f;
  _smoothingCoeff = 0.000002f;  // Very slow/smooth for heavy smoothing

  // Initialiser les indices et tailles de buffer
  for (int i = 0; i < NUM_DELAY_LINES; i++) {
    _delayIndices[i] = 0;
    _delaySizes[i] = ZITA_PRIME_DELAYS[i];
    _lpSamples[i] = 0.0f;
  }

  _preDelayIndex = 0;
  _preDelaySize = (int)(0.1f * _sampleRate); // Max 100ms

  // Mise à jour des filtres
  updateReverbParameters();
}

ZitaRev1::~ZitaRev1() {}

void ZitaRev1::init(float sampleRate) {
  _sampleRate = sampleRate;
  updateReverbParameters();
  log_info("AUDIO", "ZitaRev1 Reverb initialized at %.0f Hz", sampleRate);
}

void ZitaRev1::clear() {
  memset(_delayLines, 0, sizeof(float) * NUM_DELAY_LINES * MAX_DELAY_SIZE);
  memset(_preDelayBuffer, 0, sizeof(float) * MAX_PREDELAY_SIZE);

  for (int i = 0; i < NUM_DELAY_LINES; i++) {
    _lpSamples[i] = 0.0f;
  }
}

void ZitaRev1::setParameter(int index, float value) {
  if (index >= 0 && index < NUM_PARAMS) {
    _parameters[index] = value;
    updateReverbParameters();
  }
}

float ZitaRev1::getParameter(int index) const {
  if (index >= 0 && index < NUM_PARAMS) {
    return _parameters[index];
  }
  return 0.0f;
}

void ZitaRev1::set_roomsize(float value) { setParameter(ROOMSIZE, value); }
void ZitaRev1::set_damping(float value) { setParameter(DAMPING, value); }
void ZitaRev1::set_width(float value) { setParameter(WIDTH, value); }
void ZitaRev1::set_delay(float value) { setParameter(PREDELAY, value); }
void ZitaRev1::set_mix(float value) { setParameter(MIX, value); }

float ZitaRev1::get_roomsize() const { return _parameters[ROOMSIZE]; }
float ZitaRev1::get_damping() const { return _parameters[DAMPING]; }
float ZitaRev1::get_width() const { return _parameters[WIDTH]; }
float ZitaRev1::get_mix() const { return _parameters[MIX]; }

void ZitaRev1::process(float *inputL, float *inputR, float *outputL,
                       float *outputR, unsigned int numSamples) {
  // Paramètres locaux pour performance
  const float width = _parameters[WIDTH];

#if DEBUG_AUDIO_REVERB
  static int zita_debug_counter = 0;
  if (++zita_debug_counter >= 4800) {
    zita_debug_counter = 0;
    printf("ZITA DEBUG: mix=%.3f, width=%.3f, roomsize=%.3f, damping=%.3f, gain0=%.6f\n",
           mix, width, _parameters[ROOMSIZE], _parameters[DAMPING], _gain0);
  }
#endif

  // Pour chaque échantillon
  for (unsigned int i = 0; i < numSamples; i++) {
    // Smooth gain0 towards target for click-free parameter changes
    _currentGain0 += (_targetGain0 - _currentGain0) * _smoothingCoeff;
    
    // Mixer les entrées
    float input = (inputL[i] + inputR[i]) * 0.5f;

    // Appliquer le pré-delay
    _preDelayBuffer[_preDelayIndex] = input;

    int readIndex =
        _preDelayIndex - (int)(_preDelaySize * _parameters[PREDELAY]);
    if (readIndex < 0) {
      readIndex += _preDelaySize;
    }

    float preDelayed = _preDelayBuffer[readIndex];

    // Mettre à jour l'index du pré-delay
    _preDelayIndex = (_preDelayIndex + 1) % _preDelaySize;

    // Variables pour accumuler les réflexions gauche et droite
    float leftReflections = 0.0f;
    float rightReflections = 0.0f;

    // Première moitié des lignes de délai pour le canal gauche
    for (int j = 0; j < NUM_DELAY_LINES / 2; j++) {
      // Lire l'échantillon actuel
      float delaySample = readDelay(j);

      // Filtre passe-bas pour simuler l'absorption de l'air
      float dampingFactor = 0.2f + _parameters[DAMPING] * 0.8f;
      _lpSamples[j] =
          delaySample * dampingFactor + _lpSamples[j] * (1.0f - dampingFactor);

      // Appliquer la réverbération et le feedback (using smoothed gain)
      float processed = _lpSamples[j] * _currentGain0;

      // Ajouter au canal gauche
      leftReflections += processed;

      // Écrire dans la ligne de délai avec un mélange de l'entrée et du
      // feedback (increased to 0.9f for extreme reverb tail - very close to instability limit)
      writeDelay(j, preDelayed + processed * 0.9f);
    }

    // Deuxième moitié des lignes de délai pour le canal droit
    for (int j = NUM_DELAY_LINES / 2; j < NUM_DELAY_LINES; j++) {
      // Lire l'échantillon actuel
      float delaySample = readDelay(j);

      // Filtre passe-bas pour simuler l'absorption de l'air
      float dampingFactor = 0.2f + _parameters[DAMPING] * 0.8f;
      _lpSamples[j] =
          delaySample * dampingFactor + _lpSamples[j] * (1.0f - dampingFactor);

      // Appliquer la réverbération et le feedback (using smoothed gain)
      float processed = _lpSamples[j] * _currentGain0;

      // Ajouter au canal droit
      rightReflections += processed;

      // Écrire dans la ligne de délai avec un mélange de l'entrée et du
      // feedback (increased to 0.9f for extreme reverb tail - very close to instability limit)
      writeDelay(j, preDelayed + processed * 0.9f);
    }

    // No attenuation - unity gain (1.0x)
    // Dry/wet mixing is handled externally in audio_rtaudio.cpp
    // leftReflections and rightReflections are used as-is

#if DEBUG_AUDIO_REVERB
    static int zita_sample_counter = 0;
    if (++zita_sample_counter >= 4800) {
      zita_sample_counter = 0;
      printf("ZITA PROCESS: input=%.6f, preDelayed=%.6f, leftRefl=%.6f, rightRefl=%.6f\n",
             input, preDelayed, leftReflections, rightReflections);
    }
#endif

    // Appliquer la largeur stéréo
    float centerComponent = (leftReflections + rightReflections) * 0.7071f;
    float sideComponent = (leftReflections - rightReflections) * width;

    // CRITICAL FIX: Normalize output to prevent gain accumulation from feedback loops
    // The 8 delay lines with 0.9 feedback can cause signal buildup
    // Empirically determined compensation factor to maintain unity gain
    const float OUTPUT_COMPENSATION = 0.25f;  // Divide by ~4 to compensate for 8 delay lines
    
    // Output ONLY wet signal (100% wet) - dry/wet mixing is handled externally in audio_rtaudio.cpp
    // This prevents double mixing which causes volume jumps
    outputL[i] = (centerComponent + sideComponent) * OUTPUT_COMPENSATION;
    outputR[i] = (centerComponent - sideComponent) * OUTPUT_COMPENSATION;
  }
}

float ZitaRev1::readDelay(int line) {
  return _delayLines[line][_delayIndices[line]];
}

void ZitaRev1::writeDelay(int line, float sample) {
  _delayLines[line][_delayIndices[line]] = sample;
  _delayIndices[line] = (_delayIndices[line] + 1) % _delaySizes[line];
}

void ZitaRev1::updateReverbParameters() {
  // Mettre à jour les tailles des lignes de délai en fonction de la taille de
  // la pièce
  float sizeAdjust = 0.4f + _parameters[ROOMSIZE] * 0.6f;
  for (int i = 0; i < NUM_DELAY_LINES; i++) {
    int size = (int)(ZITA_PRIME_DELAYS[i] * sizeAdjust);
    if (size > MAX_DELAY_SIZE) {
      size = MAX_DELAY_SIZE;
    }
    _delaySizes[i] = size;
  }

  // Recalculer le pré-delay (max 100ms)
  _preDelaySize = (int)(0.1f * _sampleRate);
  if (_preDelaySize > MAX_PREDELAY_SIZE) {
    _preDelaySize = MAX_PREDELAY_SIZE;
  }

  // Calculer le temps de réverbération (2.0s à 8.0s for extreme reverb)
  // Previous: 0.5s to 3.0s was too short, 1.0s to 5.0s still not enough
  float revTime = 2.0f + 6.0f * _parameters[ROOMSIZE];

  // Calculer le gain interne en fonction du temps de réverbération
  // Set TARGET gain instead of direct gain for smooth transitions
  _targetGain0 = pow(0.001f, 1.0f / (revTime * _sampleRate));
  
  // Keep _gain0 for backward compatibility (deprecated)
  _gain0 = _targetGain0;

  // Mettre à jour les gains de mixage
  _gain1 = 1.0f - _parameters[MIX]; // Dry gain
  _gain2 = _parameters[MIX];        // Wet gain
}
