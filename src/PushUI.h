#pragma once

#include <vector>
#include <map>
#include <chrono>
#include <functional>
#include <memory>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <iostream>

#include "OSCSender.h"

#include "PushUSB.h"
//#include "ResolumeTrackerREST.h"
#include "ResolumeTrackerOSC.h"

// Main PushUI class
class PushUI {
private:
    PushUSB& pushDevice;
    ResolumeTracker& resolumeTracker;
    std::shared_ptr<OSCSender> oscSender; // Changed from unique_ptr
    int columnOffset;
    int layerOffset;
    int numLayers;  // Total number of layers in the current deck
    int numColumns; // Total number of columns in the current deck
    enum PushControls {
        BTN_OCTAVE_UP = 55,
        BTN_OCTAVE_DOWN = 54,
        BTN_PAGE_LEFT = 62,
        BTN_PAGE_RIGHT = 63,
        BTN_PLAY = 85,
        BTN_RECORD = 86,
        BTN_STOP = 87
    };
    int lastKnownDeck;
    bool trackingInitialized;

    // Add mode enum and member
    enum class Mode {
        Triggering,
        Selecting
    };
    Mode mode = Mode::Triggering;

    // Encoder position tracking (0.0 = minimum, 1.0 = maximum)
    float encoderPositions[8];

public:
    PushUI(PushUSB& push, ResolumeTracker& tracker, std::shared_ptr<OSCSender> osc = nullptr);
    ~PushUI();
    bool initialize();
    void onMidiMessage(const PushMidiMessage& msg);
    
    // Public getters for PushDisplay and PushLights to read state
    Mode getMode() const { return mode; }
    float getEncoderPosition(int index) const { 
        if (index >= 0 && index < 8) return encoderPositions[index];
        return 0.0f;
    }
    int getColumnOffset() const { return columnOffset; }
    int getLayerOffset() const { return layerOffset; }
    int getNumLayers() const { return numLayers; }
    int getNumColumns() const { return numColumns; }
    ResolumeTracker& getResolumeTracker() { return resolumeTracker; }
    PushUSB& getPushDevice() { return pushDevice; } // For lights that need direct device access
    OSCSender* getOSCSender() const { return oscSender.get(); }

    // Mode accessors
    void setMode(Mode m) { mode = m; }
    void toggleMode();

    // Encoder position tracking methods
    void updateEncoderPosition(int encoderIndex, int relativeValue);
    void handleEncoderTouch(int encoderIndex);
    void handleEncoderButtonPress(int encoderIndex);

private:
    void handlePadPress(int note, int velocity);
    void handleNavigationButtons(int controller, int value);
    void handleTouchStripPitchBend(uint16_t pitchBendValue);
};

