#include "PushUI.h"
#include <iostream>

PushUI::PushUI(PushUSB& push, ResolumeTracker& tracker, std::shared_ptr<OSCSender> osc)
    : pushDevice(push), resolumeTracker(tracker), oscSender(osc),
      columnOffset(0), layerOffset(0),
      lastKnownDeck(-1), trackingInitialized(false),
      numLayers(0), numColumns(0), mode(Mode::Triggering)
{
    // Initialize encoder positions to center (0.5)
    for (int i = 0; i < 8; i++) {
        encoderPositions[i] = 0.5f;
    }
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
        // Handle encoder touch events (notes 71-78)
        if (msg.getNote() >= 71 && msg.getNote() <= 78 && msg.getVelocity() > 0) {
            int encoderIndex = msg.getNote() - 71;
            handleEncoderTouch(encoderIndex);
            return;
        }
        
        handlePadPress(msg.getNote(), msg.getVelocity());
    } else if (msg.isPitchBend()) {
        handleTouchStripPitchBend(msg.getPitchBend());
    } else if (msg.isControlChange()) {
        int cc = msg.getController();
        int value = msg.getValue();

        // Handle encoder position updates (CC71-CC78)
        if (cc >= 71 && cc <= 78) {
            int encoderIndex = cc - 71;
            updateEncoderPosition(encoderIndex, value);
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
    int selectedLayer = resolumeTracker.getSelectedLayerId();
    if (selectedLayer <= 0) {
        return; // No layer selected
    }
    
    // Convert 14-bit pitch bend value (0-16383) to normalized position (0.0-1.0)
    float normalizedPosition = static_cast<float>(pitchBendValue) / 16383.0f;
    
    float opacity;
    
    // Create sensitive middle region with clipped top and bottom fourths
    if (normalizedPosition <= 0.25f) {
        // Bottom fourth - clamp to 0
        opacity = 0.0f;
    } else if (normalizedPosition >= 0.75f) {
        // Top fourth - clamp to 1
        opacity = 1.0f;
    } else {
        // Middle half (0.25 to 0.75) - map to full range (0.0 to 1.0)
        opacity = (normalizedPosition - 0.25f) / 0.5f;
    }
    
    // Clamp to valid range (should already be in range, but safety check)
    opacity = std::max(0.0f, std::min(1.0f, opacity));
    
    // Send OSC message to set layer opacity
    std::string address = "/composition/selectedlayer/video/opacity";
    if (oscSender) {
        oscSender->sendMessage(address, opacity);
    } else {
        std::cout << "Would set layer " << selectedLayer << " opacity to: " << opacity << std::endl;
    }
}

void PushUI::updateEncoderPosition(int encoderIndex, int relativeValue) {
    if (encoderIndex < 0 || encoderIndex >= 8) return;
    
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
    encoderPositions[encoderIndex] += deltaPosition;
    encoderPositions[encoderIndex] = std::max(0.0f, std::min(1.0f, encoderPositions[encoderIndex]));
}

void PushUI::handleEncoderTouch(int encoderIndex) {
    if (encoderIndex < 0 || encoderIndex >= 8) return;
    
    // TODO: Handle encoder touch event
    // This is called when encoder is touched (note on message)
    std::cout << "Encoder " << encoderIndex << " touched, position: " << encoderPositions[encoderIndex] << std::endl;
}

void PushUI::handleEncoderButtonPress(int encoderIndex) {
    if (encoderIndex < 0 || encoderIndex >= 8) return;
    
    // TODO: Handle encoder button press event  
    // This is called when button above encoder is pressed (CC102-109)
    std::cout << "Encoder button " << encoderIndex << " pressed, position: " << encoderPositions[encoderIndex] << std::endl;
}

/*
inline bool canMoveLayerUp() { return layerOffset + 8 < numLayers; };
inline bool canMoveLayerDown() { return layerOffset > 0; };
inline bool canMoveColumnRight() { return columnOffset + 8 < numColumns; };
inline bool canMoveColumnLeft() { return columnOffset > 0; };
*/