// ZitaRev1.h
// --------
// Open source implementation of Fons Adriaensen's zita-rev1 reverb
// Algorithm adapted for CISYNTH
//
// Original algorithm by Fons Adriaensen <fons@linuxaudio.org>
// C++ implementation based on PelleJuul's version
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.

#ifndef ZITAREV1_H
#define ZITAREV1_H

#include <algorithm>
#include <cmath>
#include <cstring>

// Constantes pour les délais (nombres premiers)
static const int ZITA_PRIME_DELAYS[8] = {743,  809,  877,  947,
                                         1019, 1097, 1171, 1259};

class ZitaRev1 {
public:
  enum {
    ROOMSIZE = 0, // 0-1: taille de la pièce (affecte le temps de réverbération)
    DAMPING = 1,  // 0-1: amortissement des hautes fréquences
    WIDTH = 2,    // 0-1: largeur stéréo
    PREDELAY = 3, // 0-1: délai initial
    MIX = 4,      // 0-1: mix dry/wet
    NUM_PARAMS
  };

public:
  ZitaRev1() {
    // Initialisation des paramètres
    for (int i = 0; i < NUM_PARAMS; i++)
      mParameters[i] = 0.0f;

    // Valeurs par défaut
    mParameters[ROOMSIZE] = 0.7f;
    mParameters[DAMPING] = 0.5f;
    mParameters[WIDTH] = 1.0f;
    mParameters[PREDELAY] = 0.02f;
    mParameters[MIX] = 0.5f;

    // Initialisation des buffers
    memset(mDelayLines, 0, sizeof(float) * NUM_DELAY_LINES * MAX_DELAY_SIZE);
    memset(mPreDelayBuffer, 0, sizeof(float) * MAX_PREDELAY_SIZE);

    // Autres paramètres
    mSampleRate = 44100.0f;
    mGain0 = 1.0f;                    // Gain interne de la réverbération
    mGain1 = 1.0f - mParameters[MIX]; // Dry gain
    mGain2 = mParameters[MIX];        // Wet gain

    // Initialiser les indices et tailles de buffer
    for (int i = 0; i < NUM_DELAY_LINES; i++) {
      mDelayIndices[i] = 0;
      mDelaySizes[i] = ZITA_PRIME_DELAYS[i];
      mLPSamples[i] = 0.0f;
    }

    mPreDelayIndex = 0;
    mPreDelaySize = (int)(0.1f * mSampleRate); // Max 100ms

    // Mise à jour des filtres
    update();
  }

  ~ZitaRev1() {}

  // Initialiser le taux d'échantillonnage
  void init(float sampleRate) {
    mSampleRate = sampleRate;
    update();
  }

  // Effacer tous les buffers
  void clear() {
    memset(mDelayLines, 0, sizeof(float) * NUM_DELAY_LINES * MAX_DELAY_SIZE);
    memset(mPreDelayBuffer, 0, sizeof(float) * MAX_PREDELAY_SIZE);

    for (int i = 0; i < NUM_DELAY_LINES; i++) {
      mLPSamples[i] = 0.0f;
    }
  }

  // Régler un paramètre par son indice
  void setParameter(int index, float value) {
    if (index >= 0 && index < NUM_PARAMS) {
      mParameters[index] = value;
      update();
    }
  }

  // Getters pour les paramètres
  float getParameter(int index) const {
    if (index >= 0 && index < NUM_PARAMS) {
      return mParameters[index];
    }
    return 0.0f;
  }

  // Raccourcis pour les paramètres principaux
  void set_roomsize(float value) { setParameter(ROOMSIZE, value); }
  void set_damping(float value) { setParameter(DAMPING, value); }
  void set_width(float value) { setParameter(WIDTH, value); }
  void set_delay(float value) { setParameter(PREDELAY, value); }
  void set_mix(float value) { setParameter(MIX, value); }

  // Getters pour les paramètres principaux
  float get_roomsize() const { return mParameters[ROOMSIZE]; }
  float get_damping() const { return mParameters[DAMPING]; }
  float get_width() const { return mParameters[WIDTH]; }
  float get_mix() const { return mParameters[MIX]; }

  // Traitement audio
  void process(float *inputL, float *inputR, float *outputL, float *outputR,
               unsigned int numSamples) {
    // Paramètres locaux pour performance
    const float mix = mParameters[MIX];
    const float width = mParameters[WIDTH];
    const float g1 = 1.0f - mix; // Gain dry
    const float g2 = mix;        // Gain wet

    // Pour chaque échantillon
    for (unsigned int i = 0; i < numSamples; i++) {
      // Mixer les entrées
      float input = (inputL[i] + inputR[i]) * 0.5f;

      // Appliquer le pré-delay
      mPreDelayBuffer[mPreDelayIndex] = input;

      int readIndex = mPreDelayIndex - mPreDelaySize * mParameters[PREDELAY];
      if (readIndex < 0) {
        readIndex += mPreDelaySize;
      }

      float preDelayed = mPreDelayBuffer[readIndex];

      // Mettre à jour l'index du pré-delay
      mPreDelayIndex = (mPreDelayIndex + 1) % mPreDelaySize;

      // Variables pour accumuler les réflexions gauche et droite
      float leftReflections = 0.0f;
      float rightReflections = 0.0f;

      // Première moitié des lignes de délai pour le canal gauche
      for (int j = 0; j < NUM_DELAY_LINES / 2; j++) {
        // Lire l'échantillon actuel
        float delaySample = readDelay(j);

        // Filtre passe-bas pour simuler l'absorption de l'air
        float dampingFactor = 0.2f + mParameters[DAMPING] * 0.8f;
        mLPSamples[j] = delaySample * dampingFactor +
                        mLPSamples[j] * (1.0f - dampingFactor);

        // Appliquer la réverbération et le feedback
        float processed = mLPSamples[j] * mGain0;

        // Ajouter au canal gauche
        leftReflections += processed;

        // Écrire dans la ligne de délai avec un mélange de l'entrée et du
        // feedback
        writeDelay(j, preDelayed + processed * 0.5f);
      }

      // Deuxième moitié des lignes de délai pour le canal droit
      for (int j = NUM_DELAY_LINES / 2; j < NUM_DELAY_LINES; j++) {
        // Lire l'échantillon actuel
        float delaySample = readDelay(j);

        // Filtre passe-bas pour simuler l'absorption de l'air
        float dampingFactor = 0.2f + mParameters[DAMPING] * 0.8f;
        mLPSamples[j] = delaySample * dampingFactor +
                        mLPSamples[j] * (1.0f - dampingFactor);

        // Appliquer la réverbération et le feedback
        float processed = mLPSamples[j] * mGain0;

        // Ajouter au canal droit
        rightReflections += processed;

        // Écrire dans la ligne de délai avec un mélange de l'entrée et du
        // feedback
        writeDelay(j, preDelayed + processed * 0.5f);
      }

      // Normaliser les réflexions
      leftReflections *= 0.25f;
      rightReflections *= 0.25f;

      // Appliquer la largeur stéréo
      float centerComponent = (leftReflections + rightReflections) * 0.7071f;
      float sideComponent = (leftReflections - rightReflections) * width;

      // Mixer les signaux secs et traités
      outputL[i] = inputL[i] * g1 + (centerComponent + sideComponent) * g2;
      outputR[i] = inputR[i] * g1 + (centerComponent - sideComponent) * g2;
    }
  }

private:
  // Constantes
  static const int MAX_DELAY_SIZE = 8192;    // Taille max des lignes de délai
  static const int MAX_PREDELAY_SIZE = 4800; // 100ms @ 48kHz
  static const int NUM_DELAY_LINES = 8;      // Nombre de lignes de délai

  // Paramètres
  float mParameters[NUM_PARAMS];
  float mSampleRate;

  // Gains
  float mGain0; // Gain interne de la réverbération
  float mGain1; // Dry gain
  float mGain2; // Wet gain

  // Buffers de délai
  float mDelayLines[NUM_DELAY_LINES][MAX_DELAY_SIZE];
  int mDelayIndices[NUM_DELAY_LINES];
  int mDelaySizes[NUM_DELAY_LINES];
  float mLPSamples[NUM_DELAY_LINES]; // Échantillons filtrés (passe-bas)

  // Buffer de pré-delay
  float mPreDelayBuffer[MAX_PREDELAY_SIZE];
  int mPreDelayIndex;
  int mPreDelaySize;

  // Lire à partir d'une ligne de délai
  float readDelay(int line) { return mDelayLines[line][mDelayIndices[line]]; }

  // Écrire dans une ligne de délai
  void writeDelay(int line, float sample) {
    mDelayLines[line][mDelayIndices[line]] = sample;
    mDelayIndices[line] = (mDelayIndices[line] + 1) % mDelaySizes[line];
  }

  // Mettre à jour les paramètres internes
  void update() {
    // Mettre à jour les tailles des lignes de délai en fonction de la taille de
    // la pièce
    float sizeAdjust = 0.4f + mParameters[ROOMSIZE] * 0.6f;
    for (int i = 0; i < NUM_DELAY_LINES; i++) {
      int size = (int)(ZITA_PRIME_DELAYS[i] * sizeAdjust);
      if (size > MAX_DELAY_SIZE) {
        size = MAX_DELAY_SIZE;
      }
      mDelaySizes[i] = size;
    }

    // Recalculer le pré-delay (max 100ms)
    mPreDelaySize = (int)(0.1f * mSampleRate);
    if (mPreDelaySize > MAX_PREDELAY_SIZE) {
      mPreDelaySize = MAX_PREDELAY_SIZE;
    }

    // Calculer le temps de réverbération (0.5s à 3.0s)
    float revTime = 0.5f + 2.5f * mParameters[ROOMSIZE];

    // Calculer le gain interne en fonction du temps de réverbération
    mGain0 = pow(0.001f, 1.0f / (revTime * mSampleRate));

    // Mettre à jour les gains de mixage
    mGain1 = 1.0f - mParameters[MIX]; // Dry gain
    mGain2 = mParameters[MIX];        // Wet gain
  }
};

#endif // ZITAREV1_H
