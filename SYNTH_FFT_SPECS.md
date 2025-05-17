# Spécification Fonctionnelle : Synthétiseur Additif Modulé par FFT

## 1. Vue d'Ensemble

Le système est un synthétiseur polyphonique (5 voix) basé sur la synthèse additive. Chaque voix est composée de 30 oscillateurs sinusoïdaux. Le timbre de chaque note est dynamiquement sculpté par les magnitudes issues d'une Transformée de Fourier Rapide (FFT) en temps réel, calculée à partir d'une source de données externe (actuellement, des données d'image). Le synthétiseur est contrôlé via MIDI pour les notes, les enveloppes, le filtre et les effets de modulation.

## 2. Moteur de Synthèse Principal

*   **Polyphonie :** Le synthétiseur pourra jouer jusqu'à **5 notes simultanément** (5 voix de polyphonie).
*   **Oscillateurs par Voix :** Chaque voix comprend **30 oscillateurs à onde sinusoïdale**.
    *   **Oscillateur 1 (Fondamental) :** Sa fréquence (`f0`) est déterminée par la note MIDI jouée.
    *   **Oscillateurs 2 à 30 (Harmoniques) :** Leurs fréquences sont des multiples entiers de `f0` (2*`f0`, 3*`f0`, ..., 30*`f0`).
*   **Modulation Timbrale par FFT :**
    *   **Source FFT :** Les magnitudes sont issues de l'analyse FFT temps réel (taille `CIS_MAX_PIXELS_NB = 3456`) du flux de données d'image, tel qu'implémenté dans `synth_fft.c`.
    *   **Bins FFT Utilisés :** Les 30 premiers bins de magnitude (Bin 0 à Bin 29) sont utilisés.
    *   **Mapping des Bins aux Amplitudes des Oscillateurs :**
        *   La magnitude du `Bin[0]` de la FFT module l'amplitude de l'oscillateur fondamental.
        *   La magnitude du `Bin[k]` (pour k de 1 à 29) module l'amplitude du (k+1)-ième oscillateur (c'est-à-dire le k-ième harmonique).
    *   **Normalisation des Magnitudes FFT pour l'Amplitude :**
        *   Pour l'oscillateur fondamental (piloté par `Bin[0]`) :
            `amplitude_normalisee = magnitude_brute_bin0 / 881280.0`
        *   Pour les oscillateurs harmoniques (pilotés par `Bin[k]`, k=1 à 29) :
            `amplitude_normalisee = min(1.0f, magnitude_brute_binK / 220320.0)`
        *   L'amplitude de chaque oscillateur est directement cette valeur normalisée.

## 3. Chaîne de Signal par Voix

Pour chaque voix active, le signal est traité comme suit :

1.  **Banque d'Oscillateurs :**
    *   Les 30 oscillateurs sinusoïdaux génèrent leurs signaux.
    *   L'amplitude de chaque oscillateur est déterminée par la magnitude FFT normalisée correspondante.
    *   Les signaux des 30 oscillateurs sont sommés : `signal_somme_osc = somme(oscillateur_i * amplitude_normalisee_i)`.
2.  **Enveloppe de Volume ADSR :**
    *   Type : ADSR (Attack, Decay, Sustain, Release).
    *   Contrôle : Module l'amplitude du `signal_somme_osc`.
    *   Déclenchement : Par les messages MIDI Note On (début Attack) et Note Off (début Release).
    *   `signal_post_vol_adsr = signal_somme_osc * valeur_actuelle_adsr_volume`.
3.  **Filtre Passe-Bas :**
    *   Type : Filtre passe-bas (par exemple, Butterworth 2 pôles ou similaire).
    *   Application : Sur le `signal_post_vol_adsr`.
    *   Fréquence de Coupure (Cutoff) : Une valeur de base, modulée par l'enveloppe ADSR de filtre.
4.  **Enveloppe de Filtre ADSR :**
    *   Type : ADSR.
    *   Contrôle : Module la fréquence de coupure du filtre passe-bas.
    *   Déclenchement : Par les messages MIDI Note On et Note Off.
    *   `signal_final_voix = filtre_passe_bas(signal_post_vol_adsr, cutoff_base + (valeur_actuelle_adsr_filtre * profondeur_enveloppe_filtre))`.

## 4. Modules Globaux et Effets

*   **Vibrato (LFO) :**
    *   Un LFO (Low-Frequency Oscillator) à onde sinusoïdale module la hauteur (fréquence) de tous les oscillateurs (fondamental et harmoniques) d'une voix active de manière cohérente.
    *   Paramètres : Fréquence du LFO (vitesse du vibrato) et Amplitude du LFO (profondeur du vibrato).
*   **Sortie Master :** Le signal final est la somme des signaux de toutes les voix actives.

## 5. Contrôle MIDI

Le synthétiseur sera contrôlable via un clavier MIDI (comme le Launchkey Mini).

*   **Messages Note On / Note Off :**
    *   Déclenchement et gestion des voix (allocation/libération).
    *   Détermination de la fréquence fondamentale (`f0`) de la voix.
    *   Déclenchement des enveloppes ADSR de volume et de filtre.
    *   La vélocité de la note pourra influencer l'amplitude initiale de l'enveloppe de volume et/ou la profondeur de l'enveloppe de filtre.
*   **Pitch Bend :** Modifie la hauteur de toutes les voix actives.
*   **Messages de Control Change (CC) :** Permettront de contrôler en temps réel :
    *   Paramètres des ADSR de Volume (Attack, Decay, Sustain, Release).
    *   Paramètres des ADSR de Filtre (Attack, Decay, Sustain, Release, Profondeur d'enveloppe).
    *   Fréquence de coupure de base du filtre.
    *   Résonance du filtre (si le type de filtre choisi le supporte).
    *   Fréquence (Rate) du LFO de vibrato.
    *   Amplitude (Depth) du LFO de vibrato.
    *   (Optionnel) Volume Master.

## 6. Traitement Audio et FFT

*   **Fréquence d'Échantillonnage :** Définie par `SAMPLING_FREQUENCY` dans `config.h` (actuellement 96000 Hz).
*   **Taille du Buffer Audio :** Définie par `AUDIO_BUFFER_SIZE` dans `config.h` (actuellement 512 échantillons).
*   La source des données pour la FFT et le calcul des magnitudes restent tels qu'implémentés dans `synth_fft.c`.

## 7. Interface Utilisateur

*   Aucune interface graphique (GUI) n'est prévue dans cette spécification initiale. Le contrôle se fait via MIDI.
*   Des messages de débogage (par exemple, l'affichage des magnitudes FFT) pourront être conservés ou adaptés.
