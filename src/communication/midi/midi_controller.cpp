/*
 * midi_controller.cpp
 *
 * Created on: 15 May 2025
 * Author: Sp3ctra Team
 */

#include "midi_controller.h"
#include "midi_mapping.h"
#include "audio_rtaudio.h"
#include "config.h"
#include "../../config/config_audio.h" // For DEFAULT_REVERB_SEND_* defines
#include "synth_additive.h" // For synth data freeze global variables and mutex
#include "synth_polyphonic.h" // For synth_polyphonic_set_vibrato_rate
#include "three_band_eq.h"
#include <algorithm>
#include <iostream>
#include <pthread.h> // Required for pthread_mutex_lock/unlock

// Global instance
MidiController *gMidiController = nullptr;

// Global flag to enable/disable unified MIDI system
// Set to 1 to use new system, 0 to use legacy hardcoded mappings (deprecated)
static int g_use_unified_midi_system = 1;

MidiController::MidiController()
    : midiIn(nullptr), isConnected(false), currentController(MIDI_NONE) {
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

  // ============================================================================
  // NEW UNIFIED MIDI SYSTEM - Priority dispatch
  // ============================================================================
  if (g_use_unified_midi_system) {
    unsigned char messageType = status & 0xF0;
    unsigned char channel = status & 0x0F;
    
    // Convert RtMidi message type to our enum and dispatch
    if (messageType == 0xB0) {
      // Control Change
      midi_mapping_dispatch(MIDI_MSG_CC, channel, number, value);
      return; // Skip legacy processing
      
    } else if (messageType == 0x90) {
      // Note On
      midi_mapping_dispatch(MIDI_MSG_NOTE_ON, channel, number, value);
      
      // Also call legacy callbacks if registered
      if (noteOnCallback) {
        if (value > 0) { // Real note on
          noteOnCallback(number, value);
        } else { // Note on with velocity 0 = note off
          if (noteOffCallback) {
            noteOffCallback(number);
          }
        }
      }
      return; // Skip legacy processing
      
    } else if (messageType == 0x80) {
      // Note Off
      midi_mapping_dispatch(MIDI_MSG_NOTE_OFF, channel, number, value);
      
      // Also call legacy callback if registered
      if (noteOffCallback) {
        noteOffCallback(number);
      }
      return; // Skip legacy processing
      
    } else if (messageType == 0xE0) {
      // Pitch Bend
      midi_mapping_dispatch(MIDI_MSG_PITCHBEND, channel, number, value);
      return; // Skip legacy processing
    }
    
    // For other message types, fall through to legacy system
  }
  
  // ============================================================================
  // LEGACY SYSTEM REMOVED - All MIDI processing now handled by unified system
  // ============================================================================
  // If unified system is disabled and we reach here, log a warning
  std::cerr << "Warning: Unified MIDI system disabled but no legacy handler available. "
            << "Enable unified system (g_use_unified_midi_system = 1)." << std::endl;
}

float MidiController::convertCCToVolume(unsigned char value) {
  // Convert from MIDI value (0-127) to normalized volume (0.0-1.0)
  return static_cast<float>(value) / 127.0f;
}

// ============================================================================
// C API functions for compatibility with existing code
// ============================================================================

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

void midi_SetupVolumeControl() {
  if (gMidiController && gAudioSystem) {
    gMidiController->setVolumeChangeCallback([](float volume) {
      if (gAudioSystem) {
        gAudioSystem->setMasterVolume(volume);
      }
    });
    printf("MIDI volume control enabled\n");
  } else {
    printf("Cannot setup MIDI volume control - MIDI or Audio not initialized\n");
  }
}

void midi_set_note_on_callback(void (*callback)(int noteNumber, int velocity)) {
  if (gMidiController) {
    gMidiController->setNoteOnCallback(callback);
  }
}

void midi_set_note_off_callback(void (*callback)(int noteNumber)) {
  if (gMidiController) {
    gMidiController->setNoteOffCallback(callback);
  }
}

} // extern "C"
