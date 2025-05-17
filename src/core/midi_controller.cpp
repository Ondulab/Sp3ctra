/*
 * midi_controller.cpp
 *
 * Created on: 15 May 2025
 * Author: CISYNTH Team
 */

#include "midi_controller.h"
#include "audio_rtaudio.h"
#include "config.h"
#include "synth_fft.h" // For synth_fft_set_vibrato_rate
#include "three_band_eq.h"
#include <algorithm>
#include <iostream>

// Global instance
MidiController *gMidiController = nullptr;

// MIDI Controller CC numbers for Launchkey Mini MK3
#define LAUNCHKEY_MINI_CC_VOLUME 7 // Volume standard (0-127)
#define LAUNCHKEY_MINI_MK3_CC_MOD                                              \
  1 // Le vrai numéro de contrôleur de la molette de modulation sur MK3
#define NANOKONTROL2_CC_VOLUME 0 // Bank Select (pour nanoKONTROL2)

// Numéros CC pour la réverbération (nanoKONTROL2)
#define LAUNCHKEY_MINI_CC_REVERB_MIX 20   // Dry/Wet mix
#define LAUNCHKEY_MINI_CC_REVERB_SIZE 21  // Room size
#define LAUNCHKEY_MINI_CC_REVERB_DAMP 22  // Damping
#define LAUNCHKEY_MINI_CC_REVERB_WIDTH 23 // Largeur stéréo

// Numéros CC pour les niveaux d'envoi vers la réverbération par synthétiseur
#define CC_REVERB_SEND_SYNTH_FFT                                               \
  4 // Niveau d'envoi de réverb pour synth_fft (selon demande)
#define CC_REVERB_SEND_SYNTH_IFFT 5 // Niveau d'envoi de réverb pour synth

// Numéros CC pour l'égaliseur (nanoKONTROL2)
#define NANOKONTROL2_CC_EQ_LOW 16      // LF gain (General Purpose 1)
#define NANOKONTROL2_CC_EQ_MID 17      // Mid gain (General Purpose 2)
#define NANOKONTROL2_CC_EQ_HIGH 18     // HF gain (General Purpose 3)
#define NANOKONTROL2_CC_EQ_MID_FREQ 19 // Mid frequency (General Purpose 4)

MidiController::MidiController()
    : midiIn(nullptr), isConnected(false), currentController(MIDI_NONE),
      mix_level_synth_ifft(0.0f), // Default IFFT level to 0.0f as requested
      mix_level_synth_fft(0.5f),  // Default FFT level to 0.5f for audibility
      reverb_send_synth_ifft(0.7f),
      reverb_send_synth_fft(0.0f), // Initialize all levels
      lfo_vibrato_speed(0.5f),     // Default LFO vibrato speed (normalized 0-1)
      // ADSR values now in seconds (20ms to 2s). Default to mid-range (1.01s)
      envelope_fft_attack(0.02f +
                          0.5f * 1.98f), // Default FFT envelope attack (1.01s)
      envelope_fft_decay(0.02f +
                         0.5f * 1.98f), // Default FFT envelope decay (1.01s)
      envelope_fft_release(
          0.02f + 0.5f * 1.98f) { // Default FFT envelope release (1.01s)

  // Initialize with empty callback
  volumeChangeCallback = [](float /*volume*/) {};
  noteOnCallback = nullptr;
  noteOffCallback = nullptr;
}

MidiController::~MidiController() { cleanup(); }

bool MidiController::initialize() {
  try {
    // Create RtMidiIn instance
    midiIn = new RtMidiIn();
    return true;
  } catch (RtMidiError &error) {
    std::cerr << "Error initializing MIDI: " << error.getMessage() << std::endl;
    return false;
  }
}

void MidiController::cleanup() {
  disconnect();

  if (midiIn) {
    delete midiIn;
    midiIn = nullptr;
  }

  isConnected = false;
  currentController = MIDI_NONE;
}

bool MidiController::connect() {
  // Try to find and connect to Launchkey Mini
  if (!midiIn) {
    return false;
  }

  unsigned int nPorts = midiIn->getPortCount();

#ifdef DEBUG_MIDI
  std::cout << "MIDI: Searching for Launchkey Mini among " << nPorts
            << " devices" << std::endl;
#endif

  // Check for various possible names of the Launchkey Mini and nanoKONTROL2
  std::vector<std::string> controllerNames = {
      "Launchkey Mini",      "MIDIIN2 (Launchkey Mini)",
      "Launchkey Mini MK3",  "Launchkey Mini MIDI Port",
      "nanoKONTROL2",        "KORG nanoKONTROL2",
      "nanoKONTROL2 MIDI 1", "nanoKONTROL2 CTRL"};

  for (unsigned int i = 0; i < nPorts; i++) {
    std::string portName = midiIn->getPortName(i);

#ifdef DEBUG_MIDI
    std::cout << "MIDI device " << i << ": " << portName << std::endl;
#endif

    // Check if port name contains any of the supported controller variations
    for (const auto &name : controllerNames) {
      if (portName.find(name) != std::string::npos) {
#ifdef DEBUG_MIDI
        std::cout << "Found MIDI controller: " << portName << std::endl;
#endif
        return connectToDevice(i);
      }
    }
  }

#ifdef DEBUG_MIDI
  std::cout << "No supported MIDI controller found" << std::endl;
#endif
  return false;
}

bool MidiController::connectToDevice(unsigned int portNumber) {
  if (!midiIn) {
    return false;
  }

  try {
    // Close any existing connections
    disconnect();

    // Open the port
    midiIn->openPort(portNumber);

    // Don't ignore sysex, timing, or active sensing messages
    midiIn->ignoreTypes(false, false, false);

    // Set our callback function
    midiIn->setCallback(&MidiController::midiCallback, this);

    isConnected = true;

    // Try to identify the controller type
    std::string portName = midiIn->getPortName(portNumber);
    if (portName.find("Launchkey Mini") != std::string::npos) {
      currentController = MIDI_LAUNCHKEY_MINI;
    } else if (portName.find("nanoKONTROL2") != std::string::npos) {
      currentController = MIDI_NANO_KONTROL2; // Correctly identify nanoKONTROL2
    } else {
      currentController = MIDI_NONE;
    }

    std::cout << "Connected to MIDI device: " << portName << std::endl;
    return true;
  } catch (RtMidiError &error) {
    std::cerr << "Error connecting to MIDI device: " << error.getMessage()
              << std::endl;
    return false;
  }
}

bool MidiController::connectToDeviceByName(const std::string &deviceName) {
  if (!midiIn) {
    return false;
  }

  unsigned int nPorts = midiIn->getPortCount();

  for (unsigned int i = 0; i < nPorts; i++) {
    std::string portName = midiIn->getPortName(i);

    if (portName.find(deviceName) != std::string::npos) {
      return connectToDevice(i);
    }
  }

  return false;
}

void MidiController::disconnect() {
  if (midiIn && isConnected) {
    midiIn->closePort();
    isConnected = false;
    currentController = MIDI_NONE;
    std::cout << "MIDI device disconnected" << std::endl;
  }
}

std::vector<std::string> MidiController::getAvailableDevices() {
  std::vector<std::string> devices;

  if (!midiIn) {
    return devices;
  }

  unsigned int nPorts = midiIn->getPortCount();

  for (unsigned int i = 0; i < nPorts; i++) {
    devices.push_back(midiIn->getPortName(i));
  }

  return devices;
}

void MidiController::setVolumeChangeCallback(
    std::function<void(float)> callback) {
  volumeChangeCallback = callback;
}

void MidiController::setNoteOnCallback(
    std::function<void(int noteNumber, int velocity)> callback) {
  this->noteOnCallback = callback;
}

void MidiController::setNoteOffCallback(
    std::function<void(int noteNumber)> callback) {
  this->noteOffCallback = callback;
}

bool MidiController::isControllerConnected(MidiControllerType type) {
  return isConnected && (currentController == type);
}

bool MidiController::isAnyControllerConnected() { return isConnected; }

MidiControllerType MidiController::getCurrentControllerType() {
  return currentController;
}

std::string MidiController::getCurrentControllerName() {
  if (!isConnected) {
    return "Not connected";
  }

  switch (currentController) {
  case MIDI_LAUNCHKEY_MINI:
    return "Launchkey Mini MK3";
  case MIDI_NANO_KONTROL2:
    return "nanoKONTROL2";
  default:
    return "Unknown controller";
  }
}

// Static callback function that routes to the appropriate instance method
void MidiController::midiCallback(double timeStamp,
                                  std::vector<unsigned char> *message,
                                  void *userData) {
  MidiController *controller = static_cast<MidiController *>(userData);
  controller->processMidiMessage(timeStamp, message);
}

void MidiController::processMidiMessage(double timeStamp,
                                        std::vector<unsigned char> *message) {
  (void)timeStamp; // Mark timeStamp as unused
  // Check if this is a valid message
  if (message->size() < 3) {
    return; // Not enough data for a CC message - silently ignore
  }

  unsigned char status = message->at(0);
  unsigned char number = message->at(1);
  unsigned char value = message->at(2);

  /* Suppression des logs MIDI généraux pour ne conserver que les messages
   * spécifiques aux changements de paramètres */

  // Get channel number (0-15)
  unsigned char channel = status & 0x0F;

  unsigned char messageType = status & 0xF0;
  unsigned char midiChannel = status & 0x0F; // MIDI channel 0-15

  // Check if this is a Control Change message (0xB0-0xBF)
  if (messageType == 0xB0) {
    // Convertir en valeur normalisée (0.0 - 1.0)
    float normalizedValue = static_cast<float>(value) / 127.0f;

    // Traiter en fonction du numéro de CC
    switch (number) {
    // Volume principal (Molette de modulation CC 1, ou autres CC de volume)
    case LAUNCHKEY_MINI_MK3_CC_MOD: // CC 1 for Mod Wheel
    case LAUNCHKEY_MINI_CC_VOLUME:  // Standard CC 7 for volume
    case NANOKONTROL2_CC_VOLUME: {  // Specific to nanoKONTROL2
      // Convertir avec la fonction originale pour préserver le comportement
      // exact
      float volume = convertCCToVolume(value);
      // Call the volume change callback
      volumeChangeCallback(volume);

      // Log pour le débogage avec couleur blanche
      std::cout << "\033[1;37mVOLUME: " << (int)(volume * 100) << "%\033[0m"
                << std::endl;
    } break;

    // Réverbération: Mix Dry/Wet
    case LAUNCHKEY_MINI_CC_REVERB_MIX:
      if (gAudioSystem) {
        // Activer la réverbération si ce n'est pas déjà fait
        if (!gAudioSystem->isReverbEnabled()) {
          gAudioSystem->enableReverb(true);
        }
        gAudioSystem->setReverbMix(normalizedValue);

        // Log pour le débogage avec couleur cyan
        std::cout << "\033[1;36mREVERB MIX: " << (int)(normalizedValue * 100)
                  << "%\033[0m" << std::endl;
      }
      break;

    // Égaliseur: Gain des basses (LF)
    case NANOKONTROL2_CC_EQ_LOW:
      if (gEqualizer) {
        // Activer l'égaliseur si ce n'est pas déjà fait
        if (!gEqualizer->isEnabled()) {
          gEqualizer->setEnabled(true);
        }

        // Convertir la valeur MIDI en gain en dB (-24dB à +24dB)
        float lowGain = ((normalizedValue * 2.0f) - 1.0f) * 24.0f;
        gEqualizer->setLowGain(lowGain);

        // Log pour le débogage avec couleur verte
        std::cout << "\033[1;32mEQ LOW GAIN: " << lowGain << " dB\033[0m"
                  << std::endl;
      }
      break;

    // Égaliseur: Gain des médiums (Mid)
    case NANOKONTROL2_CC_EQ_MID:
      if (gEqualizer) {
        // Activer l'égaliseur si ce n'est pas déjà fait
        if (!gEqualizer->isEnabled()) {
          gEqualizer->setEnabled(true);
        }

        // Convertir la valeur MIDI en gain en dB (-24dB à +24dB)
        float midGain = ((normalizedValue * 2.0f) - 1.0f) * 24.0f;
        gEqualizer->setMidGain(midGain);

        // Log pour le débogage avec couleur bleu clair
        std::cout << "\033[1;36mEQ MID GAIN: " << midGain << " dB\033[0m"
                  << std::endl;
      }
      break;

    // Égaliseur: Gain des aigus (HF)
    case NANOKONTROL2_CC_EQ_HIGH:
      if (gEqualizer) {
        // Activer l'égaliseur si ce n'est pas déjà fait
        if (!gEqualizer->isEnabled()) {
          gEqualizer->setEnabled(true);
        }

        // Convertir la valeur MIDI en gain en dB (-24dB à +24dB)
        float highGain = ((normalizedValue * 2.0f) - 1.0f) * 24.0f;
        gEqualizer->setHighGain(highGain);

        // Log pour le débogage avec couleur violet
        std::cout << "\033[1;35mEQ HIGH GAIN: " << highGain << " dB\033[0m"
                  << std::endl;
      }
      break;

    // Égaliseur: Fréquence des médiums
    case NANOKONTROL2_CC_EQ_MID_FREQ:
      if (gEqualizer) {
        // Activer l'égaliseur si ce n'est pas déjà fait
        if (!gEqualizer->isEnabled()) {
          gEqualizer->setEnabled(true);
        }

        // Convertir la valeur MIDI en fréquence (250Hz à 5000Hz, échelle
        // logarithmique)
        float midFreq = 250.0f * powf(20.0f, normalizedValue);
        gEqualizer->setMidFrequency(midFreq);

        // Log pour le débogage avec couleur jaune
        std::cout << "\033[1;33mEQ MID FREQ: " << midFreq << " Hz\033[0m"
                  << std::endl;
      }
      break;

    // Volume synthèse IFFT (CC 21)
    case MIDI_CC_IFFT_VOLUME:
      mix_level_synth_ifft = normalizedValue;
      std::cout << "\033[1;37mIFFT SYNTH VOLUME: "
                << (int)(normalizedValue * 100) << "%\033[0m" << std::endl;
      break;

    // Volume synthèse FFT (CC 22)
    case MIDI_CC_FFT_VOLUME:
      mix_level_synth_fft = normalizedValue;
      std::cout << "\033[1;37mFFT SYNTH VOLUME: "
                << (int)(normalizedValue * 100) << "%\033[0m" << std::endl;
      break;

    // Wet/Dry réverbération FFT (CC 23)
    case MIDI_CC_REVERB_WET_DRY_FFT:
      reverb_send_synth_fft = normalizedValue;
      std::cout << "\033[1;36mFFT REVERB WET/DRY: "
                << (int)(normalizedValue * 100) << "%\033[0m" << std::endl;
      if (gAudioSystem && normalizedValue > 0.0f) {
        if (!gAudioSystem->isReverbEnabled()) {
          gAudioSystem->enableReverb(true);
        }
      }
      break;

    // Wet/Dry réverbération IFFT (CC 24)
    case MIDI_CC_REVERB_WET_DRY_IFFT:
      reverb_send_synth_ifft = normalizedValue;
      std::cout << "\033[1;36mIFFT REVERB WET/DRY: "
                << (int)(normalizedValue * 100) << "%\033[0m" << std::endl;
      if (gAudioSystem && normalizedValue > 0.0f) {
        if (!gAudioSystem->isReverbEnabled()) {
          gAudioSystem->enableReverb(true);
        }
      }
      break;

    // Vitesse LFO vibrato (CC 25)
    case MIDI_CC_LFO_VIBRATO_SPEED:
      // Scale normalizedValue (0.0-1.0) to a sensible LFO rate range (e.g., 0.1
      // Hz to 10 Hz) The actual scaling factor (9.9f here) can be adjusted
      // based on desired max LFO speed. Minimum LFO speed is 0.1 Hz.
      lfo_vibrato_speed = 0.1f + normalizedValue * 9.9f;
      synth_fft_set_vibrato_rate(
          lfo_vibrato_speed); // Call the function to update LFO rate
      std::cout << "\033[1;35mVIBRATO LFO SPEED: " << lfo_vibrato_speed
                << " Hz\033[0m" << std::endl;
      break;

    // Attack enveloppe FFT (CC 26)
    case MIDI_CC_ENVELOPE_FFT_ATTACK:
      // Scale normalizedValue (0.0-1.0) to 0.02s-2.0s
      envelope_fft_attack = 0.02f + normalizedValue * 1.98f;
      synth_fft_set_volume_adsr_attack(envelope_fft_attack); // Apply to synth
      std::cout << "\033[1;33mFFT ENV ATTACK: "
                << (int)(envelope_fft_attack * 1000) << " ms\033[0m"
                << std::endl;
      break;

    // Decay enveloppe FFT (CC 27)
    case MIDI_CC_ENVELOPE_FFT_DECAY:
      // Scale normalizedValue (0.0-1.0) to 0.02s-2.0s
      envelope_fft_decay = 0.02f + normalizedValue * 1.98f;
      synth_fft_set_volume_adsr_decay(envelope_fft_decay); // Apply to synth
      std::cout << "\033[1;33mFFT ENV DECAY: "
                << (int)(envelope_fft_decay * 1000) << " ms\033[0m"
                << std::endl;
      break;

    // Release enveloppe FFT (CC 28)
    case MIDI_CC_ENVELOPE_FFT_RELEASE:
      // Scale normalizedValue (0.0-1.0) to 0.02s-2.0s
      envelope_fft_release = 0.02f + normalizedValue * 1.98f;
      synth_fft_set_volume_adsr_release(envelope_fft_release); // Apply to synth
      std::cout << "\033[1;33mFFT ENV RELEASE: "
                << (int)(envelope_fft_release * 1000) << " ms\033[0m"
                << std::endl;
      break;

    // Legacy CC mappings (can be removed if nanoKONTROL2 sliders are
    // re-assigned) nanoKONTROL2 SLIDER/KNOB Control 1 Breath Control (coarse)
    // -> CC 2
    case 2: // Kept for backward compatibility or other controllers
      // This might conflict with MIDI_CC_IFFT_VOLUME if not careful
      // mix_level_synth_ifft = normalizedValue;
      // std::cout << "\033[1;37m(Legacy CC2) MIX IFFT: " <<
      // (int)(normalizedValue * 100)
      //           << "%\033[0m" << std::endl;
      break;

    // nanoKONTROL2 SLIDER/KNOB Control 1 Controller 3 -> CC 3
    case 3: // Kept for backward compatibility or other controllers
      // This might conflict with MIDI_CC_FFT_VOLUME if not careful
      // mix_level_synth_fft = normalizedValue;
      // std::cout << "\033[1;37m(Legacy CC3) MIX FFT: " <<
      // (int)(normalizedValue * 100)
      //           << "%\033[0m" << std::endl;
      break;

    // Contrôleur pour le niveau d'envoi de réverb pour synth FFT (Legacy CC 4)
    case CC_REVERB_SEND_SYNTH_FFT: // Kept for backward compatibility
      // This might conflict with MIDI_CC_REVERB_WET_DRY_FFT
      // reverb_send_synth_fft = normalizedValue;
      // std::cout << "\033[1;36m(Legacy CC4) REVERB SEND FFT: " <<
      // (int)(normalizedValue*100) << "%\033[0m" << std::endl; if (gAudioSystem
      // && normalizedValue > 0.0f) {
      //     if (!gAudioSystem->isReverbEnabled()) {
      //         gAudioSystem->enableReverb(true);
      //     }
      // }
      break;

    // Contrôleur pour le niveau d'envoi de réverb pour synth IFFT (Legacy CC 5)
    case CC_REVERB_SEND_SYNTH_IFFT: // Kept for backward compatibility
      // This might conflict with MIDI_CC_REVERB_WET_DRY_IFFT
      // reverb_send_synth_ifft = normalizedValue;
      // std::cout << "\033[1;36m(Legacy CC5) REVERB SEND IFFT: " <<
      // (int)(normalizedValue*100) << "%\033[0m" << std::endl; if (gAudioSystem
      // && normalizedValue > 0.0f) {
      //     if (!gAudioSystem->isReverbEnabled()) {
      //         gAudioSystem->enableReverb(true);
      //     }
      // }
      break;

    // Autres contrôleurs non gérés
    default:
#ifdef DEBUG_MIDI
      std::cout << "MIDI CC: Channel=" << (int)channel
                << ", Controller=" << (int)number << ", Value=" << (int)value
                << ", Normalized=" << normalizedValue << std::endl;
#endif
      break;
    }
  } else if (messageType == 0x90) { // Note On message
    int noteNumber = number;        // message->at(1) is 'number'
    int velocity = value;           // message->at(2) is 'value'
    // #ifdef DEBUG_MIDI // Commenting out these general Note On/Off logs
    //     std::cout << "MIDI Note On: Channel=" << (int)midiChannel
    //               << ", Note=" << noteNumber << ", Velocity=" << velocity
    //               << std::endl;
    // #endif
    if (velocity > 0) {
      if (noteOnCallback) {
        noteOnCallback(noteNumber, velocity);
      }
    } else { // Velocity 0 is typically a Note Off
      if (noteOffCallback) {
        noteOffCallback(noteNumber);
      }
    }
  } else if (messageType == 0x80) { // Note Off message
    int noteNumber = number;        // message->at(1) is 'number'
    // int velocity = value; // Velocity for note off
    // #ifdef DEBUG_MIDI // Commenting out these general Note On/Off logs
    //     std::cout << "MIDI Note Off: Channel=" << (int)midiChannel
    //               << ", Note=" << noteNumber << ", Velocity=" << (int)value
    //               << std::endl;
    // #endif
    if (noteOffCallback) {
      noteOffCallback(noteNumber);
    }
  }
  // Other MIDI messages (Pitch Bend, Program Change, etc.) can be handled here
}

float MidiController::convertCCToVolume(unsigned char value) {
  // Convert from MIDI value (0-127) to normalized volume (0.0-1.0)
  return static_cast<float>(value) / 127.0f;
}

// Accessors for mix levels
float MidiController::getMixLevelSynthIfft() const {
  return mix_level_synth_ifft;
}

float MidiController::getMixLevelSynthFft() const {
  return mix_level_synth_fft;
}

// Accessors for reverb send levels
float MidiController::getReverbSendSynthIfft() const {
  return reverb_send_synth_ifft;
}

float MidiController::getReverbSendSynthFft() const {
  return reverb_send_synth_fft;
}

// Accessors for new MIDI controls
float MidiController::getLfoVibratoSpeed() const { return lfo_vibrato_speed; }

float MidiController::getEnvelopeFftAttack() const {
  return envelope_fft_attack;
}

float MidiController::getEnvelopeFftDecay() const { return envelope_fft_decay; }

float MidiController::getEnvelopeFftRelease() const {
  return envelope_fft_release;
}

// C API functions for compatibility with existing code
extern "C" {

void midi_Init() {
  if (!gMidiController) {
    gMidiController = new MidiController();
    if (gMidiController) {
      gMidiController->initialize();
    }
  }
}

void midi_Cleanup() {
  if (gMidiController) {
    delete gMidiController;
    gMidiController = nullptr;
  }
}

int midi_Connect() {
  if (gMidiController) {
    return gMidiController->connect() ? 1 : 0;
  }
  return 0;
}

void midi_Disconnect() {
  if (gMidiController) {
    gMidiController->disconnect();
  }
}

// Fonction spéciale pour définir le callback de volume pour le mode CLI
void midi_SetupVolumeControl() {
  if (gMidiController && gAudioSystem) {
    // Définir le callback pour le contrôle du volume
    gMidiController->setVolumeChangeCallback([](float volume) {
      // Mettre à jour le volume principal dans le système audio
      if (gAudioSystem) {
        gAudioSystem->setMasterVolume(volume);

        // Ne plus afficher de message pour chaque changement de volume
        // printf("Volume principal: %d%%\n", (int)(volume * 100));
      }
    });

    printf("MIDI volume control enabled\n");
  } else {
    printf(
        "Cannot setup MIDI volume control - MIDI or Audio not initialized\n");
  }
}

// Implementation of C-wrapper functions for setting note callbacks
void midi_set_note_on_callback(void (*callback)(int noteNumber, int velocity)) {
  if (gMidiController) {
    // The std::function can wrap a C function pointer
    gMidiController->setNoteOnCallback(callback);
  }
}

void midi_set_note_off_callback(void (*callback)(int noteNumber)) {
  if (gMidiController) {
    // The std::function can wrap a C function pointer
    gMidiController->setNoteOffCallback(callback);
  }
}

} // extern "C"
