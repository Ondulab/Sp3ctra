// ZitaRev1.cpp
// --------
// Implémentation de l'adaptateur pour intégrer zita-rev1 original dans CISYNTH
//
// Original algorithm by Fons Adriaensen <fons@linuxaudio.org>
// C++ implementation by PelleJuul
// Adapter implementation for CISYNTH
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.

#include "ZitaRev1.h"
#include <cmath>
#include <cstring>
#include <iostream>

ZitaRev1::ZitaRev1() {
  // Initialisation des paramètres à des valeurs par défaut
  for (int i = 0; i < NUM_PARAMS; i++) {
    _parameters[i] = 0.0f;
  }

  // Valeurs par défaut (identiques à l'implémentation actuelle)
  _parameters[ROOMSIZE] = 0.7f;
  _parameters[DAMPING] = 0.5f;
  _parameters[WIDTH] = 1.0f;
  _parameters[PREDELAY] = 0.02f;
  _parameters[MIX] = 0.5f;

  _sampleRate = 44100.0f;
  _ambisonic = false; // Nous n'utilisons pas le mode ambisonique
}

ZitaRev1::~ZitaRev1() {
  // Rien à faire ici, les ressources sont libérées automatiquement
}

void ZitaRev1::init(float sampleRate) {
  _sampleRate = sampleRate;
  _reverb.init(sampleRate, _ambisonic);

  // Initialiser les paramètres de la reverb originale
  updateReverbParameters();

  std::cout << "\033[1;32m[ZitaRev1] Reverb initialisée à " << sampleRate
            << " Hz\033[0m" << std::endl;
}

void ZitaRev1::clear() {
  // Pas d'équivalent direct dans l'implémentation originale
  // Nous pouvons le simuler en réinitialisant la reverb
  _reverb.fini();
  _reverb.init(_sampleRate, _ambisonic);
  updateReverbParameters();
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
  // Conversion entre l'API CISYNTH et l'API originale
  // L'implémentation originale utilise des tableaux de pointeurs
  float *inputs[2] = {inputL, inputR};
  float *outputs[4] = {
      outputL, outputR, nullptr,
      nullptr}; // Les deux derniers canaux ne sont utilisés qu'en ambisonique

  // Mise à jour des paramètres si nécessaire
  _reverb.prepare(numSamples);

  // Process avec l'implémentation originale
  _reverb.process(numSamples, inputs, outputs);
}

void ZitaRev1::updateReverbParameters() {
  // Mappage des paramètres normalisés (0-1) vers les paramètres de
  // l'implémentation originale

  // 1. Délai initial (0-100ms)
  float delay = _parameters[PREDELAY] * 0.1f; // Jusqu'à 100ms
  _reverb.set_delay(delay);

  // 2. Taille de pièce/Temps de réverbération (0.5s-5s)
  float rtlow =
      0.5f +
      4.5f * _parameters[ROOMSIZE]; // Temps de réverbération basses fréquences
  float rtmid =
      rtlow *
      0.8f; // Temps de réverbération moyennes fréquences un peu plus court
  _reverb.set_rtlow(rtlow);
  _reverb.set_rtmid(rtmid);

  // 3. Amortissement des hautes fréquences (1kHz-10kHz)
  float fdamp =
      1000.0f + 9000.0f * _parameters[DAMPING]; // Fréquence de coupure
  _reverb.set_fdamp(fdamp);

  // 4. Largeur stéréo (0-1)
  // Pour l'implémentation originale, le paramètre opmix contrôle partiellement
  // la largeur stéréo
  float opmix = 0.5f + 0.5f * _parameters[WIDTH];
  _reverb.set_opmix(opmix);

  // 5. Mix dry/wet est géré dans l'implémentation originale directement dans
  // process Nous pouvons l'ajuster via le paramètre rgxyz (bien que ce ne soit
  // pas son but premier)
  float rgxyz = _parameters[MIX] * 2.0f - 1.0f; // Convertir 0-1 en -1 à 1
  _reverb.set_rgxyz(rgxyz);

  // 6. Fréquence de crossover
  _reverb.set_xover(200.0f); // Crossover fixe à 200Hz pour simplifier

  // 7. Equalizers - laissés neutres pour simplifier
  _reverb.set_eq1(160.0f, 0.0f);  // 160Hz, gain 0dB
  _reverb.set_eq2(2500.0f, 0.0f); // 2.5kHz, gain 0dB
}
