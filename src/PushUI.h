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

struct Encoder {        
    std::string displayName;     // Text to show on display
    std::string oscAddress;      // OSC address path (relative to layer/clip)
    //float minValue;              // Minimum value for this parameter
    //float maxValue;              // Maximum value for this parameter
    float physicalValue;  // always between 0 and 1
    float virtualValue; // always between 0 and 1
};

// Main PushUI class
class PushUI {
public: 
    // Add mode enum and member
    enum class Mode {
        Triggering,
        Selecting
    };

private:
    PushUSB& pushDevice;
    ResolumeTracker& resolumeTracker;
    std::shared_ptr<OSCSender> oscSender; // Changed from unique_ptr
    int columnOffset;
    int layerOffset;
    //int numLayers;  // Total number of layers in the current deck
    //int numColumns; // Total number of columns in the current deck
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

    Mode mode = Mode::Triggering;
    Encoder encoders[11]; // there are 11 encoders, 8 above the display, one for setup/user, one for metronome, and one detent one for tempo.

public:
    PushUI(PushUSB& push, ResolumeTracker& tracker, std::shared_ptr<OSCSender> osc = nullptr);
    ~PushUI();
    bool initialize();
    void onMidiMessage(const PushMidiMessage& msg);
    
    // Public getters for PushDisplay and PushLights to read state
    Mode getMode() const { return mode; }

    Encoder getEncoder(int index) const { 
        if (index >= 0 && index < 8) return encoders[index];
        return Encoder{};
    }

    float getEncoderPosition(int index) const { 
        if (index >= 0 && index < 8) return encoders[index].physicalValue;
        return 0.0f;
    }
    float getEncoderVirtualValue(int index) const {
        if (index >= 0 && index < 8) return encoders[index].virtualValue;
        return 0.0f;
    }
    int getColumnOffset() const { return columnOffset; }
    int getLayerOffset() const { return layerOffset; }
    int getNumLayers() const { return resolumeTracker.getLayerCount(); }
    int getNumColumns() const { return resolumeTracker.getColumnCount(); }
    float getCrossfaderPosition() const { return resolumeTracker.getCrossfaderPosition(); }
    ResolumeTracker& getResolumeTracker() { return resolumeTracker; }
    PushUSB& getPushDevice() { return pushDevice; } // For lights that need direct device access
    OSCSender* getOSCSender() const { return oscSender.get(); }

    // Mode accessors
    void setMode(Mode m) { mode = m; }
    void toggleMode();

    // Encoder position tracking methods
    void updateEncoderPhysicalValue(int encoderIndex, int relativeValue);
    void handleEncoderTouch(int encoderIndex);
    void handleEncoderButtonPress(int encoderIndex);
    
    // Callback functions for managing display and encoder values
    void onSelectedItemChange();
    void updateEncoderVirtualValues();

private:
    void handlePadPress(int note, int velocity);
    void handleNavigationButtons(int controller, int value);
    void handleTouchStripPitchBend(uint16_t pitchBendValue);

    // Encoder assignment methods
    void initializeDefaultEncoderAssignments();
    void reassignEncodersForEffects();
    //std::vector<std::string> getActiveEffectsFromProperties(PropertyDictionary& properties);
    //std::vector<std::string> getSortedEffectsPriority(const std::vector<std::string>& activeEffects);
    //EncoderAssignment createAssignmentForEffect(const std::string& effectName);
};

