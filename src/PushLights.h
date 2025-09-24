#pragma once

#include "PushUSB.h"
#include "Color.h"
#include <map>

#define PALETTE_BLACK 0
#define PALETTE_RGB_WHITE 122
#define PALETTE_BW_WHITE 127
#define PALETTE_SIZE 128

class PushUI; // Forward declaration

// PushLights class - handles all LED lighting
class PushLights {
private:
    PushUSB& pushDevice;
    PushUI* parentUI;
    static const int PAD_ROWS = 8;
    static const int PAD_COLS = 8;
    static const int FIRST_PAD_NOTE = 36;
    // Use a fixed array for 64 pads
    uint8_t currentPadPaletteIndices[64] = {0};
    // Use a fixed array for all buttons (cc0-cc119)
    uint8_t currentButtonPaletteIndices[120] = {0};
    // Touchstrip LED state (31 LEDs, values 0-7)
    uint8_t currentTouchStripLEDs[31] = {0};
    bool lightsInitialized;

    // Unified palette: index -> {r,g,b,w}
    struct PaletteEntry {
        uint8_t r, g, b, w;
    };

    PaletteEntry palette[PALETTE_SIZE] = {0};

    //static constexpr uint8_t MAX_CUSTOM_PALETTE_INDEX = 121; // 122+ reserved

    // Helper: is this button RGB?
    static inline bool isRGBButton(int cc) {
        return
            (cc >= 102 && cc <= 109) ||
            (cc >= 20 && cc <= 27) ||
            (cc >= 36 && cc <= 43) ||
            cc == 60 || cc == 61 || cc == 29 ||
            cc == 85 || cc == 86 || cc == 89;
    }

    // Helper: get nearest palette index for BW brightness (0-128)
    uint8_t getBWPaletteIndex(uint8_t brightness) {
        // Search for an entry with matching w (ignore r,g,b)
        for (uint8_t idx = 0; idx < PALETTE_SIZE; ++idx) {
            if (palette[idx].w == brightness) {
                return idx;
            }
        }
        // SKIP the first index because its always 0 and should remain that way.
        for (uint8_t idx = 1; idx < PALETTE_SIZE; ++idx) {
            if (palette[idx].w == 0) { // reuse an entry with no bw value
                pushDevice.setPaletteEntry(idx, palette[idx].r, palette[idx].g, palette[idx].b, brightness);
                palette[idx].w = brightness;
                std::cout << "PushLights: Reused palette entry " << (int)idx << " for custom BW value " << (int)brightness << std::endl;
                return idx;
            }
        }

        std::cerr << "PushLights: Out of palette indices for custom BW values!" << std::endl;
        return 0;
    }

    // Unified: get or create palette index for RGB color, preserving W if already present
    uint8_t getRGBPaletteIndex(const Color& color) {
        // Search for an entry with matching r,g,b (ignore w)
        for (uint8_t idx = 0; idx < PALETTE_SIZE; ++idx) {
            if (palette[idx].r == color.r && palette[idx].g == color.g && palette[idx].b == color.b) {
                return idx;
            }
        }

        // SKIP the first index because its always 0 and should remain that way.
        for (uint8_t idx = 1; idx < PALETTE_SIZE; idx++) {
            //std::cout << "Checking palette index " << (int)idx << ": (" << (int)palette[idx].r << ", " << (int)palette[idx].g << ", " << (int)palette[idx].b << ", " << (int)palette[idx].w << ")" << std::endl;
            if (palette[idx].r == 0 && palette[idx].g == 0 && palette[idx].b == 0) {
                pushDevice.setPaletteEntry(idx, color.r, color.g, color.b, palette[idx].w);
                palette[idx].r = color.r;
                palette[idx].g = color.g;
                palette[idx].b = color.b;
                //std::cout << "PushLights: Created custom RGB palette entry " << (int)idx << " for color (" << (int)color.r << ", " << (int)color.g << ", " << (int)color.b << ")" << std::endl;
                return idx;
            }
        }
        std::cerr << "PushLights: Out of palette indices for custom RGB values!" << std::endl;
        return 0;
    }

public:
    PushLights(PushUSB& push, PushUI* ui = nullptr) : pushDevice(push), parentUI(ui), lightsInitialized(false) {
        palette[PALETTE_BLACK]        = {0, 0, 0, 0};        // black
        palette[16]                   = {0,0,0,32};           // dark gray
        palette[48]                   = {0,0,0,84};           // light gray
        palette[PALETTE_RGB_WHITE]    = {204, 204, 204, 0};   // white
        palette[123]                  = {64, 64, 64, 0};      // rgb light gray
        palette[124]                  = {20, 20, 20, 0};      // rgb dark gray
        palette[125]                  = {0, 0, 255, 0};       // blue
        palette[126]                  = {0, 255, 0, 0};       // green
        palette[PALETTE_BW_WHITE]     = {255, 0, 0, 128};     // rgb red, bw white

        for (int i = 0; i < 64; ++i) currentPadPaletteIndices[i] = PALETTE_BLACK;
        for (int i = 0; i < 120; ++i) currentButtonPaletteIndices[i] = 0;
        for (int i = 0; i < 31; ++i) currentTouchStripLEDs[i] = 0;

        pushDevice.configureTouchStrip();
    }

    void setParentUI(PushUI* parent) { parentUI = parent; }

    // Set pad color using note number (only sends if palette index changed)
    void setPadColor(int note, const Color& color) {
        if (note < FIRST_PAD_NOTE || note > FIRST_PAD_NOTE + 63) return;
        //std::cout << "Trying to set " << note << " to color (" << (int)color.r << ", " << (int)color.g << ", " << (int)color.b << ")" << std::endl;
        uint8_t paletteIdx = getRGBPaletteIndex(color);
        //std::cout << "Got palette index " << (int)paletteIdx << std::endl;
        int idx = note - FIRST_PAD_NOTE;
        if (currentPadPaletteIndices[idx] == paletteIdx) {
            return;
        }
        pushDevice.setPadColorIndex(note, paletteIdx);
        //std::cout << "Setting Pad Color Index: " << note << ", " << (int)paletteIdx << std::endl;
        currentPadPaletteIndices[idx] = paletteIdx;
    }

    // Set pad color using row/column (0-based)
    void setPadColor(int row, int col, const Color& color) {
        if (row >= 0 && row < PAD_ROWS && col >= 0 && col < PAD_COLS) {
            int note = FIRST_PAD_NOTE + (row * PAD_COLS + col);
            setPadColor(note, color);
        }
    }

    // Set button color for BW button (brightness 0-128)
    void setButtonColorBW(int cc, uint8_t brightness) {
        if (cc < 0 || cc >= 120) return;
        if (isRGBButton(cc)) {
            std::cerr << "setButtonColorBW: cc" << cc << " is not a BW button!" << std::endl;
            return;
        }
        uint8_t paletteIdx = getBWPaletteIndex(brightness);
        
        if (currentButtonPaletteIndices[cc] == paletteIdx) return;
        pushDevice.setButtonColorIndex(cc, paletteIdx);
        //std::cout << "Setting BW Button Color Index: " << cc << ", " << paletteIdx << std::endl;
        currentButtonPaletteIndices[cc] = paletteIdx;
    }

    // Set button color for RGB button
    void setButtonColorRGB(int cc, const Color& color) {
        if (cc < 0 || cc >= 120) return;
        if (!isRGBButton(cc)) {
            std::cerr << "setButtonColorRGB: cc" << cc << " is not an RGB button!" << std::endl;
            return;
        }
        uint8_t paletteIdx = getRGBPaletteIndex(color);
        
        if (currentButtonPaletteIndices[cc] == paletteIdx) return;
        pushDevice.setButtonColorIndex(cc, paletteIdx);
        //std::cout << "Setting RGB Button Color Index: " << cc << ", " << paletteIdx << std::endl;
        currentButtonPaletteIndices[cc] = paletteIdx;
    }

    // Clear all pads to black (forces update)
    void clearAllPads() {
        for (int i = 0; i < 64; ++i) {
            currentPadPaletteIndices[i] = PALETTE_BLACK;
            pushDevice.setPadColorIndex(FIRST_PAD_NOTE + i, PALETTE_BLACK);
        }
    }

    // Clear all buttons (forces update)
    void clearAllButtons() {
        for (int cc = 0; cc < 120; ++cc) {
            if(isRGBButton(cc)) {
                setButtonColorRGB(cc, Color::BLACK);
            } else {
                setButtonColorBW(cc, 0);
            }
        }
    }

    // Set touchstrip LEDs with array of 31 values (0-7 each)
    void setTouchStripLEDs(const uint8_t ledValues[31]) {
        // Validate values are 0-7
        for (int i = 0; i < 31; ++i) {
            if (ledValues[i] > 7) {
                std::cerr << "setTouchStripLEDs: LED values must be 0-7" << std::endl;
                return;
            }
        }

        // Check if state has changed
        bool changed = false;
        for (int i = 0; i < 31; ++i) {
            if (currentTouchStripLEDs[i] != ledValues[i]) {
                changed = true;
                break;
            }
        }

        if (!changed) {
            return; // No change needed
        }

        // Update current state
        for (int i = 0; i < 31; ++i) {
            currentTouchStripLEDs[i] = ledValues[i];
        }

        // Send to device
        pushDevice.setTouchStripLEDs(ledValues);
    }

    // Set touchstrip as meter from bottom up (0.0 = off, 1.0 = full)
    void setTouchStripMeter(float level) {
        // Clamp level to 0-1 range
        level = std::max(0.0f, std::min(1.0f, level));

        uint8_t ledValues[31] = {0};

        if (level > 0.0f) {
            // Calculate how many LEDs should be lit
            float ledCount = level * 31.0f;
            int fullLEDs = static_cast<int>(ledCount);
            float remainder = ledCount - fullLEDs;

            // Light up full LEDs
            for (int i = 0; i < fullLEDs && i < 31; ++i) {
                ledValues[i] = 7; // Full brightness
            }

            // Handle partial LED at the top
            if (fullLEDs < 31 && remainder > 0.0f) {
                // Scale remainder to 1-7 range (avoid 0 for partial)
                uint8_t partialBrightness = static_cast<uint8_t>(remainder * 6.0f + 1.0f);
                ledValues[fullLEDs] = std::min(partialBrightness, static_cast<uint8_t>(7));
            }
        }

        setTouchStripLEDs(ledValues);
    }

    // Clear touchstrip (all LEDs off)
    void clearTouchStrip() {
        uint8_t ledValues[31] = {0};
        setTouchStripLEDs(ledValues);
    }

    // Force complete refresh (useful after reconnection or initialization)
    void forceRefresh() {
        for (int i = 0; i < 64; ++i) currentPadPaletteIndices[i] = PALETTE_BLACK;
        for (int i = 0; i < 120; ++i) currentButtonPaletteIndices[i] = PALETTE_BLACK;
        for (int i = 0; i < 31; ++i) currentTouchStripLEDs[i] = 0;
        lightsInitialized = false;
    }

    // Update all lights based on current Resolume state
    void updateLights() {
        if (!lightsInitialized) {
            // First time setup - clear everything to ensure known state
            clearAllPads();
            clearAllButtons();
            lightsInitialized = true;
        }

        if (!parentUI) return;
        int connectedColumn = parentUI->getResolumeTracker().getConnectedColumn();
        int selectedLayer = parentUI->getResolumeTracker().getSelectedLayerId();
        int numColumns = parentUI->getNumColumns();
        int numLayers = parentUI->getNumLayers();
        int layerOffset = parentUI->getLayerOffset();
        int columnOffset = parentUI->getColumnOffset();

        // Column buttons: cc20-cc27
        for (int i = 0; i < 8; ++i) {
            int cc = 20 + i;
            int column = columnOffset + i + 1; // 1-based column
            if (column > numColumns || numColumns == 0) {
                setButtonColorRGB(cc, Color::BLACK);
                continue;
            }
            if (column == connectedColumn) {
                setButtonColorRGB(cc, Color::WHITE); // White (palette index)
            } else {
                // Rainbow: evenly spaced hues, mapped to palette, based on total columns
                float hue = (float)(column - 1) * 360.0f / std::max(1, numColumns);
                Color c = Color::fromHSV(hue, 1.0f, 1.0f);
                setButtonColorRGB(cc, c);
            }
        }
        // Layer buttons: cc36-cc43
        for (int i = 0; i < 8; ++i) {
            int cc = 36 + i;
            int layerIdx = parentUI->getLayerOffset() + i + 1; // 1-based layer

            Color color = Color::BLACK;
            if (layerIdx <= numLayers && numLayers > 0 && parentUI->getResolumeTracker().doesLayerExist(layerIdx)) {
                auto layerObj = parentUI->getResolumeTracker().getLayer(layerIdx);
                int crossfaderGroup = layerObj->properties.getInt("crossfadergroup");

                switch (crossfaderGroup) {
                    case 1: // A
                        color = Color::fromHSV(240.0f, 1.0f, (layerIdx == selectedLayer)? 1.0f : 0.5f);
                        break;
                    case 2: // B
                        color = Color::fromHSV(330.0f, 1.0f, (layerIdx == selectedLayer)? 1.0f : 0.5f);
                        break;
                    default:
                        color = Color::fromHSV(0.0f, 0.0f, (layerIdx == selectedLayer)? 1.0f : 0.5f);
                }
            }
            setButtonColorRGB(cc, color);
        }

        if (selectedLayer > 0) {
            auto layer = parentUI->getResolumeTracker().getLayer(selectedLayer);
            if (!layer) {
                clearTouchStrip();
                return;
            }

            // Get opacity from layer properties, default to 1.0 if not found
            float opacity = layer->properties.getFloat("video/opacity", 1.0f);
            
            // Clamp opacity to valid range
            opacity = std::max(0.0f, std::min(1.0f, opacity));

            // Display opacity as meter on touchstrip
            setTouchStripMeter(opacity);
        } else {
            clearTouchStrip();
        }

        for (int gridRow = 0; gridRow < 8; gridRow++) {
            for (int gridCol = 0; gridCol < 8; gridCol++) {
                int resolumeLayer = gridRow + 1 + layerOffset;
                int resolumeColumn = gridCol + 1 + columnOffset;
                Color padColor = Color::BLACK;
                //if (parentUI->resolumeTracker.getLayer(resolumeLayer)->getPlayingId() == resolumeColumn) {
                
                if (parentUI->getResolumeTracker().doesClipExist(resolumeColumn, resolumeLayer)) {
                    padColor = Color::WHITE;
                } 

                if (parentUI->getResolumeTracker().isClipPlaying(resolumeColumn, resolumeLayer)) {
                    // Lit up according to column number (rainbow)
                    float hue = (float)(resolumeColumn - 1.0f) * 360.0f / ((float)numColumns);
                    padColor = Color::fromHSV(hue, 1.0f, 1.0f);
                } 
                setPadColor(gridRow, gridCol, padColor);
            }
        }

        // Only send MIDI if the button state has actually changed
        setButtonColorBW(55, layerOffset + 8 < numLayers ? 255 : 0);     // BTN_OCTAVE_UP
        setButtonColorBW(54, layerOffset > 0 ? 255 : 0);   // BTN_OCTAVE_DOWN
        setButtonColorBW(63, columnOffset + 8 < numColumns ? 255 : 0); // BTN_PAGE_RIGHT
        setButtonColorBW(62, columnOffset > 0 ? 255 : 0);  // BTN_PAGE_LEFT

        // Master button (cc28) always white
        setButtonColorBW(28, 128);

        //set shift and select buttons to white
        setButtonColorBW(49, 128);
        setButtonColorBW(48, 128);

        // set "setup" and "user" buttons to white
        setButtonColorBW(30, 128);
        setButtonColorBW(59, 128);

        // set tap tempo and metronome buttons to white
        setButtonColorBW(3, 128);
        setButtonColorBW(9, 128);
    }
};