/*
 * midi_controller.h
 *
 * Created on: 15 May 2025
 * Author: CISYNTH Team
 */

#ifndef MIDI_CONTROLLER_H
#define MIDI_CONTROLLER_H

#include <functional>
#include <rtmidi/RtMidi.h>
#include <string>
#include <vector>

// MIDI Controller Types
typedef enum {
  MIDI_NONE = 0,
  MIDI_LAUNCHKEY_MINI = 1,
  MIDI_NANO_KONTROL2 = 2, // Added for nanoKONTROL2
  // Other controllers can be added here
} MidiControllerType;

// Structure for MIDI CC values
typedef struct {
  unsigned char number; // CC number
  unsigned char value;  // Current value (0-127)
  std::string name;     // Human-readable name of the controller
} MidiControlValue;

class MidiController {
private:
  RtMidiIn *midiIn;
  bool isConnected;
  MidiControllerType currentController;

  // Callback function for volume change
  std::function<void(float)> volumeChangeCallback;
  // Callbacks for Note On/Off events
  std::function<void(int noteNumber, int velocity)> noteOnCallback;
  std::function<void(int noteNumber)> noteOffCallback;

  // Static callback wrapper required by RtMidi
  static void midiCallback(double timeStamp,
                           std::vector<unsigned char> *message, void *userData);

  // Process incoming MIDI messages
  void processMidiMessage(double timeStamp,
                          std::vector<unsigned char> *message);

  // Convert CC value (0-127) to volume (0.0-1.0)
  float convertCCToVolume(unsigned char value);

public:
  // Variables to store mix levels for the two synths
  float mix_level_synth_ifft;
  float mix_level_synth_fft;

  // Variables to store reverb send levels for each synth
  float reverb_send_synth_ifft;
  float reverb_send_synth_fft;

  MidiController();
  ~MidiController();

  // Initialization and cleanup
  bool initialize();
  void cleanup();

  // Connect to MIDI devices
  bool connect();
  bool connectToDevice(unsigned int portNumber);
  bool connectToDeviceByName(const std::string &deviceName);
  void disconnect();

  // Get list of available MIDI input devices
  std::vector<std::string> getAvailableDevices();

  // Set callback for volume changes
  void setVolumeChangeCallback(std::function<void(float)> callback);
  // Set callbacks for Note On/Off events
  void
  setNoteOnCallback(std::function<void(int noteNumber, int velocity)> callback);
  void setNoteOffCallback(std::function<void(int noteNumber)> callback);

  // Check if specific controller is connected
  bool isControllerConnected(MidiControllerType type);
  bool isAnyControllerConnected();

  // Get current controller information
  MidiControllerType getCurrentControllerType();
  std::string getCurrentControllerName();

  // Accessors for mix levels
  float getMixLevelSynthIfft() const;
  float getMixLevelSynthFft() const;

  // Accessors for reverb send levels
  float getReverbSendSynthIfft() const;
  float getReverbSendSynthFft() const;
};

// Global instance for C API compatibility
extern MidiController *gMidiController;

// C API functions for compatibility with existing code
extern "C" {
void midi_Init();
void midi_Cleanup();
int midi_Connect();
void midi_Disconnect();
void midi_SetupVolumeControl(); // Nouvelle fonction pour le contrôle du volume
                                // en mode CLI

// C-wrapper functions for setting note callbacks
void midi_set_note_on_callback(void (*callback)(int noteNumber, int velocity));
void midi_set_note_off_callback(void (*callback)(int noteNumber));
}

#endif /* MIDI_CONTROLLER_H */
