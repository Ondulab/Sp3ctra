/*
 * midi_controller.cpp
 *
 * Created on: 15 May 2025
 * Author: CISYNTH Team
 */

#include "midi_controller.h"
#include "audio_rtaudio.h"
#include "config.h"
#include <algorithm>
#include <iostream>

// Global instance
MidiController *gMidiController = nullptr;

// MIDI Controller CC numbers for Launchkey Mini MK3
#define LAUNCHKEY_MINI_CC_VOLUME 7 // Volume standard (0-127)
#define LAUNCHKEY_MINI_MK3_CC_MOD                                              \
  1 // Le vrai numéro de contrôleur de la molette de modulation sur MK3

// Numéros CC pour la réverbération (nanoKONTROL2)
#define LAUNCHKEY_MINI_CC_REVERB_MIX 20   // Dry/Wet mix
#define LAUNCHKEY_MINI_CC_REVERB_SIZE 21  // Room size
#define LAUNCHKEY_MINI_CC_REVERB_DAMP 22  // Damping
#define LAUNCHKEY_MINI_CC_REVERB_WIDTH 23 // Largeur stéréo

MidiController::MidiController()
    : midiIn(nullptr), isConnected(false), currentController(MIDI_NONE) {

  // Initialize with empty callback
  volumeChangeCallback = [](float volume) {};
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

  // Check for various possible names of the Launchkey Mini
  std::vector<std::string> launchkeyNames = {
      "Launchkey Mini", "MIDIIN2 (Launchkey Mini)", "Launchkey Mini MK3",
      "Launchkey Mini MIDI Port"};

  for (unsigned int i = 0; i < nPorts; i++) {
    std::string portName = midiIn->getPortName(i);

#ifdef DEBUG_MIDI
    std::cout << "MIDI device " << i << ": " << portName << std::endl;
#endif

    // Check if port name contains any of the Launchkey Mini variations
    for (const auto &name : launchkeyNames) {
      if (portName.find(name) != std::string::npos) {
#ifdef DEBUG_MIDI
        std::cout << "Found Launchkey Mini: " << portName << std::endl;
#endif
        return connectToDevice(i);
      }
    }
  }

#ifdef DEBUG_MIDI
  std::cout << "No Launchkey Mini device found" << std::endl;
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

  // Check if this is a Control Change message (0xB0-0xBF)
  if ((status & 0xF0) == 0xB0) {
    // Convertir en valeur normalisée (0.0 - 1.0)
    float normalizedValue = static_cast<float>(value) / 127.0f;

    // Traiter en fonction du numéro de CC
    switch (number) {
    // Volume principal
    case LAUNCHKEY_MINI_CC_VOLUME:
    case LAUNCHKEY_MINI_MK3_CC_MOD: {
      // Convertir avec la fonction originale pour préserver le comportement
      // exact
      float volume = convertCCToVolume(value);
      // Call the volume change callback
      volumeChangeCallback(volume);

      // Log pour le débogage supprimé (utilise uniquement le log dans
      // AudioSystem)
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

    // Réverbération: Taille de la pièce
    case LAUNCHKEY_MINI_CC_REVERB_SIZE:
      if (gAudioSystem) {
        // Activer la réverbération si ce n'est pas déjà fait
        if (!gAudioSystem->isReverbEnabled()) {
          gAudioSystem->enableReverb(true);
        }
        gAudioSystem->setReverbRoomSize(normalizedValue);

        // Log pour le débogage avec couleur magenta
        std::cout << "\033[1;35mREVERB ROOM SIZE: "
                  << (int)(normalizedValue * 100) << "%\033[0m" << std::endl;
      }
      break;

    // Réverbération: Amortissement
    case LAUNCHKEY_MINI_CC_REVERB_DAMP:
      if (gAudioSystem) {
        // Activer la réverbération si ce n'est pas déjà fait
        if (!gAudioSystem->isReverbEnabled()) {
          gAudioSystem->enableReverb(true);
        }
        gAudioSystem->setReverbDamping(normalizedValue);

        // Log pour le débogage avec couleur bleue
        std::cout << "\033[1;34mREVERB DAMPING: "
                  << (int)(normalizedValue * 100) << "%\033[0m" << std::endl;
      }
      break;

    // Réverbération: Largeur stéréo
    case LAUNCHKEY_MINI_CC_REVERB_WIDTH:
      if (gAudioSystem) {
        // Activer la réverbération si ce n'est pas déjà fait
        if (!gAudioSystem->isReverbEnabled()) {
          gAudioSystem->enableReverb(true);
        }
        gAudioSystem->setReverbWidth(normalizedValue);

        // Log pour le débogage avec couleur jaune
        std::cout << "\033[1;33mREVERB WIDTH: " << (int)(normalizedValue * 100)
                  << "%\033[0m" << std::endl;
      }
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
  }
}

float MidiController::convertCCToVolume(unsigned char value) {
  // Convert from MIDI value (0-127) to normalized volume (0.0-1.0)
  return static_cast<float>(value) / 127.0f;
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

} // extern "C"
