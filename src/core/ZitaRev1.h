// ZitaRev1.h
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

  ZitaRev1();
  ~ZitaRev1();

  void init(float sampleRate);
  void clear();

  void setParameter(int index, float value);
  float getParameter(int index) const;

  void set_roomsize(float value);
  void set_damping(float value);
  void set_width(float value);
  void set_delay(float value);
  void set_mix(float value);

  float get_roomsize() const;
  float get_damping() const;
  float get_width() const;
  float get_mix() const;

  void process(float *inputL, float *inputR, float *outputL, float *outputR,
               unsigned int numSamples);

private:
  // Constantes
  static const int MAX_DELAY_SIZE = 8192;    // Taille max des lignes de délai
  static const int MAX_PREDELAY_SIZE = 4800; // 100ms @ 48kHz
  static const int NUM_DELAY_LINES = 8;      // Nombre de lignes de délai

  // Paramètres
  float _parameters[NUM_PARAMS];
  float _sampleRate;

  // Gains
  float _gain0; // Gain interne de la réverbération
  float _gain1; // Dry gain
  float _gain2; // Wet gain

  // Buffers de délai
  float _delayLines[NUM_DELAY_LINES][MAX_DELAY_SIZE];
  int _delayIndices[NUM_DELAY_LINES];
  int _delaySizes[NUM_DELAY_LINES];
  float _lpSamples[NUM_DELAY_LINES]; // Échantillons filtrés (passe-bas)

  // Buffer de pré-delay
  float _preDelayBuffer[MAX_PREDELAY_SIZE];
  int _preDelayIndex;
  int _preDelaySize;

  // Méthodes privées
  float readDelay(int line);
  void writeDelay(int line, float sample);
  void updateReverbParameters();
};

#endif // ZITAREV1_H
