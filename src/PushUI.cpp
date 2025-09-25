#include "PushUI.h"
#include <iostream>

PushUI::PushUI(PushUSB& push, ResolumeTracker& tracker, std::shared_ptr<OSCSender> osc)
    : pushDevice(push), resolumeTracker(tracker), oscSender(osc),
      columnOffset(0), layerOffset(0),
      lastKnownDeck(-1), trackingInitialized(false),
      mode(Mode::Triggering)
{
    
    // Initialize default encoder assignments
    initializeDefaultEncoderAssignments();
}

PushUI::~PushUI() {
    // No more lights management here
}

bool PushUI::initialize() {
    if (!pushDevice.isDeviceConnected()) {
        std::cerr << "Push device not connected" << std::endl;
        return false;
    }
    pushDevice.setMidiCallback([this](const PushMidiMessage& msg) {
        this->onMidiMessage(msg);
    });
    std::cout << "PushUI initialized successfully" << std::endl;
    return true;
}

void PushUI::toggleMode() {
    if (mode == Mode::Triggering) {
        mode = Mode::Selecting;
    } else {
        mode = Mode::Triggering;
    }

    std::cout << "Mode toggled to: " << (mode == Mode::Triggering ? "Triggering" : "Selecting") << std::endl;
}

void PushUI::onMidiMessage(const PushMidiMessage& msg) {
    if (msg.isNoteOn()) {
        // Handle encoder touch events (notes 0-10)
        if (msg.getNote() >= 0 && msg.getNote() <= 10 && msg.getVelocity() > 0) {
            int encoderIndex = msg.getNote();
            handleEncoderTouch(encoderIndex);
            return;
        }
        
        handlePadPress(msg.getNote(), msg.getVelocity());
    } else if (msg.isPitchBend()) {
        handleTouchStripPitchBend(msg.getPitchBend());
    } else if (msg.isControlChange()) {
        int cc = msg.getController();
        int value = msg.getValue();

        // Handle encoder position updates
        if (cc >= 71 && cc <= 79) { // first 9 encoders
            int encoderIndex = cc - 71;
            updateEncoderPhysicalValue(encoderIndex, value);
            return;
        } else if (cc == 14) { // 10th encoder (metronome)
            int encoderIndex = 9;
            updateEncoderPhysicalValue(encoderIndex, value);
            return;
        } else if (cc == 15) { // 11th encoder (tempo)
            int encoderIndex = 10;
            updateEncoderPhysicalValue(encoderIndex, value);
            return;
        }

        // Handle encoder button presses (CC102-CC109 - buttons above display)
        if (cc >= 102 && cc <= 109 && value > 0) {
            int encoderIndex = cc - 102;
            handleEncoderButtonPress(encoderIndex);
            return;
        }

        // Master button (cc28) toggles mode
        if (cc == 28 && value > 0) {
            toggleMode();
            return;
        }

        // tap tempo and metronome (resync)
        if (cc == 3 && value > 0) {
            std::string address = "/composition/tempocontroller/tempotap";
            if (oscSender) {
                oscSender->sendMessage(address, 1);
            }
        } else if (cc == 9 && value > 0) {
            std::string address = "/composition/tempocontroller/resync";
            if (oscSender) {
                oscSender->sendMessage(address, 1);
            }
        }

        auto layer = resolumeTracker.getSelectedLayer();

        if (cc == 30 && value > 0) { // Setup button pressed
            if (layer && layer->properties.getInt("crossfadergroup") == 1) {
                // Crossfader group A button pressed
                std::string address = "/composition/selectedlayer/crossfadergroup";
                if (oscSender) {
                    oscSender->sendMessage(address, 0);
                }
            } else {
                std::string address = "/composition/selectedlayer/crossfadergroup";
                if (oscSender) {
                    oscSender->sendMessage(address, 1);
                }
            }
            return;
        } else if (cc == 59 && value > 0) { // User button pressed
            if (layer && layer->properties.getInt("crossfadergroup") == 2) {
                // Crossfader group B button pressed
                std::string address = "/composition/selectedlayer/crossfadergroup";
                if (oscSender) {
                    oscSender->sendMessage(address, 0);
                }
            } else {
                std::string address = "/composition/selectedlayer/userbutton";
                if (oscSender) {
                    oscSender->sendMessage(address, 2);
                }
            }
            return;
        }

        // Column buttons
        if (cc >= 20 && cc <= 27 && value > 0) {
            int column = columnOffset + (cc - 20) + 1;
            if (mode == Mode::Selecting) {
                std::string address = "/composition/columns/" + std::to_string(column) + "/select";
                if (oscSender) {
                    oscSender->sendMessage(address, 1);
                } else {
                    std::cout << "Would select: " << address << std::endl;
                }
            } else {
                std::string address = "/composition/columns/" + std::to_string(column) + "/connect";
                if (oscSender) {
                    oscSender->sendMessage(address, 1);
                } else {
                    std::cout << "Would trigger: " << address << std::endl;
                }
            }
            return;
        }

        // Layer buttons
        if (cc >= 36 && cc <= 43 && value > 0) {
            int layer = layerOffset + (cc - 36) + 1;
            std::string address = "/composition/layers/" + std::to_string(layer) + "/select";
            if (oscSender) {
                oscSender->sendMessage(address, 1);
            } else {
                std::cout << "Would select: " << address << std::endl;
            }
            return;
        }

        handleNavigationButtons(cc, value);
    }
}

void PushUI::handlePadPress(int note, int velocity) {
    if (note >= 36 && note <= 99) {
        int padIndex = note - 36;
        int gridRow = padIndex / 8;
        int gridCol = padIndex % 8;
        int resolumeLayer = gridRow + 1 + layerOffset;
        int resolumeColumn = gridCol + 1 + columnOffset;
        
        if (mode == Mode::Selecting) {
            // Select the clip
            std::string address = "/composition/layers/" + std::to_string(resolumeLayer) +
                                  "/clips/" + std::to_string(resolumeColumn) + "/select";
            if (oscSender) {
                oscSender->sendMessage(address, velocity ? 1 : 0);
            } else {
                std::cout << "Would select: " << address << std::endl;
            }
        } else {
            // Trigger the clip
            std::string address = "/composition/layers/" + std::to_string(resolumeLayer) +
                             "/clips/" + std::to_string(resolumeColumn) + "/connect";
            if (oscSender) {
                oscSender->sendMessage(address, velocity ? 1 : 0);
            } else {
                std::cout << "Would trigger: " << address << std::endl;
            }

            // After triggering the clip, timeout all other clips in the same layer. If done before resolume may still send messages for the clip we tried to stop
            auto layer = resolumeTracker.getLayer(resolumeLayer);
            if (layer) {
                layer->timeoutAllExcept(resolumeColumn);
            }
        }
    }
}

void PushUI::handleNavigationButtons(int controller, int value) {
    int columns = resolumeTracker.getColumnCount();
    int layers = resolumeTracker.getLayerCount();
    
    if (value == 0) return;
    if (controller == BTN_OCTAVE_UP && layerOffset + 8 < layers) {
        layerOffset++;
    } else if (controller == BTN_OCTAVE_DOWN && layerOffset > 0) {
        layerOffset--;
    } else if (controller == BTN_PAGE_RIGHT && columnOffset + 8 < columns) {
        columnOffset++;
    } else if (controller == BTN_PAGE_LEFT && columnOffset > 0) {
        columnOffset--;
    }

    int d = resolumeTracker.getCurrentDeck();

    std::string address;

    switch (controller) {
        case 49:
            // don't clear, resolumeTracker will automatically clear on deck change. 
            //std::cout << "Deck: " << d << std::endl;
            //resolumeTracker.clear();
            // send the osc message to change the deck
            if(d <= 1) break; // don't go below deck 1
            address = "/composition/decks/" + std::to_string(d-1) + "/select";
            if (oscSender) {
                oscSender->sendMessage(address, 1);
            }
            break;
        case 48:
            //std::cout << "Deck: " << d << std::endl;
            //resolumeTracker.clear();
            // send the osc message to change the deck
            address = "/composition/decks/" + std::to_string(d+1) + "/select";
            if (oscSender) {
                oscSender->sendMessage(address, 1);
            }
            break;
    }
}

void PushUI::handleTouchStripPitchBend(uint16_t pitchBendValue) {
    // Check if there's a selected layer
    //int selectedLayer = resolumeTracker.getSelectedLayerId();
    //if (selectedLayer <= 0) {
    //    return; // No layer selected
    //}
    
    // Convert 14-bit pitch bend value (0-16383) to normalized position (0.0-1.0)
    float normalizedPosition = static_cast<float>(pitchBendValue) / 16383.0f;
    
    float output_value;
    
    // Create sensitive middle region with clipped top and bottom fourths
    if (normalizedPosition <= 0.25f) {
        // Bottom fourth - clamp to 0
        output_value = 0.0f;
    } else if (normalizedPosition >= 0.75f) {
        // Top fourth - clamp to 1
        output_value = 1.0f;
    } else {
        // Middle half (0.25 to 0.75) - map to full range (0.0 to 1.0)
        output_value = (normalizedPosition - 0.25f) / 0.5f;
    }
    
    // Clamp to valid range (should already be in range, but safety check)
    output_value = std::max(0.0f, std::min(1.0f, output_value));

    // Send OSC message to set layer opacity
    std::string address = "/composition/crossfader/phase";
    if (oscSender) {
        oscSender->sendMessage(address, output_value);
    }
}

void PushUI::initializeDefaultEncoderAssignments() {
    // Encoder 0: Always transport position
    encoders[0] = {
        "Transport",
        "transport/position",
        0.0f, 0.0f // min, max, physical, virtual
    };
    
    // Encoder 1: Always speed
    encoders[1] = {
        "Speed", 
        "transport/position/behaviour/speed",
        0.0f, 0.0f
    };
    
    // Encoders 2-7: Default to disabled (none)
    for (int i = 2; i < 8; i++) {
        encoders[i] = {
            "---",
            "",
            0.0f, 0.0f
        };
    }
}

void PushUI::updateEncoderPhysicalValue(int encoderIndex, int relativeValue) {
    if (encoderIndex < 0 || encoderIndex >= 9) return;
    
    // Convert relative encoder value to position change
    float deltaPosition = 0.0f;
    
    if (relativeValue <= 63) {
        // Clockwise turn (positive values 1-63)
        deltaPosition = relativeValue * 0.01f; // Adjust sensitivity as needed
    } else {
        // Counter-clockwise turn (values 64-127, representing -64 to -1)
        deltaPosition = (relativeValue - 128) * 0.01f; // Negative delta
    }
    
    // Update position and clamp to 0.0-1.0 range
    encoders[encoderIndex].physicalValue += deltaPosition;
    encoders[encoderIndex].physicalValue = std::max(0.0f, std::min(1.0f, encoders[encoderIndex].physicalValue));
    
    // Send OSC message using dynamic assignment
    if (oscSender) {
        const auto& assignment = encoders[encoderIndex];
        
        if (assignment.oscAddress.empty()) {
            return; // Skip disabled encoders
        }

        //auto selectedLayer = resolumeTracker.getSelectedLayer();
        //auto selectedClip = resolumeTracker.getSelectedClip();
        
        std::string address;
        float value = encoders[encoderIndex].physicalValue;

        if (encoderIndex == 8) {
            return; // Not implemented yet
        } else {
            // Scale value according to assignment range
            //value = assignment.minValue + (value * (assignment.maxValue - assignment.minValue));
            
            // Determine target and build address
            address = "/composition/selectedclip/" + assignment.oscAddress;
        }
        
        if (!address.empty()) {
            oscSender->sendMessage(address, value);
            //std::cout << "Encoder " << encoderIndex << " (" << assignment.displayName << ") -> " << address << " = " << value << std::endl;
        }
    }
}

void PushUI::handleEncoderTouch(int encoderIndex) {
    if (encoderIndex < 0 || encoderIndex >= 8) return;
    
    // TODO: Handle encoder touch event
    // This is called when encoder is touched (note on message)
    std::cout << "Encoder " << encoderIndex << " touched" << std::endl;
}

void PushUI::handleEncoderButtonPress(int encoderIndex) {
    if (encoderIndex < 0 || encoderIndex >= 8) return;
    
    // TODO: Handle encoder button press event  
    // This is called when button above encoder is pressed (CC102-109)
    std::cout << "Encoder button " << encoderIndex << " pressed" << std::endl;
}

void PushUI::onSelectedItemChange() {
    // This callback is triggered when a new clip or layer is selected
    // Update encoder assignments based on the selected item
    
    auto selectedLayer = resolumeTracker.getSelectedLayer();
    auto selectedClip = resolumeTracker.getSelectedClip();
    
    if (selectedClip.first > 0 && selectedClip.second > 0) {
        // A clip is selected - prioritize clip controls
        std::cout << "Selected item changed: Clip " << selectedClip.second << " in Layer " << selectedClip.first << std::endl;
    } else if (selectedLayer) {
        // A layer is selected but no specific clip
        std::cout << "Selected item changed: Layer " << selectedLayer->id << std::endl;
    } else {
        // Nothing specifically selected
        std::cout << "Selected item changed: No specific selection" << std::endl;
    }
    
    // Force an update of virtual values
    updateEncoderVirtualValues();
}

void PushUI::updateEncoderVirtualValues() {    
    // Get the currently selected items
    //auto selectedLayer = resolumeTracker.getSelectedLayer();
    //auto selectedClip = resolumeTracker.getSelectedClip();
    //auto effectsBus = resolumeTracker.getSelectedEffectsBus();
    
    // Update virtual values based on current assignments
    for (int i = 0; i < 8; i++) {
        const auto& assignment = encoders[i];

        if (assignment.oscAddress.empty()) {
            continue;
        }
    }
}

/*
inline bool canMoveLayerUp() { return layerOffset + 8 < numLayers; };
inline bool canMoveLayerDown() { return layerOffset > 0; };
inline bool canMoveColumnRight() { return columnOffset + 8 < numColumns; };
inline bool canMoveColumnLeft() { return columnOffset > 0; };
*/