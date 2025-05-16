// ZitaRev1.h
// --------
// Adaptateur pour intégrer l'implémentation originale de zita-rev1 de PelleJuul
// tout en maintenant la compatibilité avec l'API ZitaRev1 existante dans
// CISYNTH
//
// Original algorithm by Fons Adriaensen <fons@linuxaudio.org>
// C++ implementation by PelleJuul
// Adapter implementation for CISYNTH
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
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

#include "reverb.h"

// Cette classe maintient l'API actuelle ZitaRev1 mais utilise
// l'implémentation originale de PelleJuul en interne
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

  // Interfaces identiques à l'implémentation actuelle
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
  Reverb _reverb;                // Instance de l'implémentation originale
  float _parameters[NUM_PARAMS]; // Stockage des paramètres
  float _sampleRate;
  bool _ambisonic; // Toujours false dans notre cas

  // Méthode privée pour mettre à jour les paramètres de la reverb originale
  void updateReverbParameters();
};

#endif // ZITAREV1_H
