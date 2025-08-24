#pragma once

#include "PushUSB.h"
#include "PushUI.h"
#include "ResolumeTrackerOSC.h"
#include "Color.h"
#define CANVAS_ITY_IMPLEMENTATION
#include "canvas_ity.hpp"
#include <cstdint>
#include <memory>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265
#endif

// Display constants
static const int DISPLAY_WIDTH = 960;
static const int DISPLAY_HEIGHT = 160;

// Forward declaration
class PushUI;

// PushDisplay class - handles all display rendering
class PushDisplay {
private:
    PushUSB& pushDevice;
    PushUI* parentUI;
    std::unique_ptr<canvas_ity::canvas> canvas;
    uint8_t displayBuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT * 4]; // RGBA
    
public:
    PushDisplay(PushUSB& push) : pushDevice(push), parentUI(nullptr) {
        canvas = std::make_unique<canvas_ity::canvas>(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    }
    
    void setParentUI(PushUI* parent) { parentUI = parent; }
    
    void clear() {
        // Clear the canvas to black
        canvas->set_color(canvas_ity::fill_style, 0.0f, 0.0f, 0.0f, 1.0f);
        canvas->clear_rectangle(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    }
    
    void update() {
        if (!parentUI) {
            clear();
            return;
        }
        
        // Clear to black background
        clear();
        
        // Check if we're in selecting mode
        if (parentUI->getMode() == PushUI::Mode::Selecting) {
            // Draw 2-pixel green border around entire screen
            canvas->set_color(canvas_ity::stroke_style, 0.0f, 1.0f, 0.0f, 1.0f);
            canvas->set_line_width(2.0f);
            
            // Draw rectangle border (stroke a rectangle that covers the screen)
            canvas->stroke_rectangle(1.0f, 1.0f, 
                                   static_cast<float>(DISPLAY_WIDTH - 2), 
                                   static_cast<float>(DISPLAY_HEIGHT - 2));
        }
        
        // Draw the 8 encoder knobs
        drawEncoders();
    }
    
    void sendToDevice() {
        // Get the rendered image data from canvas
        canvas->get_image_data(displayBuffer, DISPLAY_WIDTH, DISPLAY_HEIGHT, 
                             DISPLAY_WIDTH * 4, 0, 0);
                             
        if (pushDevice.isDeviceConnected()) {
            pushDevice.sendDisplayFrameBlocking(displayBuffer);
        }
    }
    
    void drawKnob(int x, int y, const std::string& textOnKnob, const std::string& textBelowKnob, 
              float currentValue, float physicalValue) {
        const float knobRadius = 25.0f;
        const float trackRadius = 32.0f;
        const float trackWidth = 4.0f;
        const float dotRadius = 3.0f;
        const float dotDistance = 18.0f;
        
        // Clamp values to 0-1 range
        currentValue = std::max(0.0f, std::min(1.0f, currentValue));
        physicalValue = std::max(0.0f, std::min(1.0f, physicalValue));
        
        // Convert values to angles (270 degrees rotation range: -135° to +135°)
        const float angleRange = 270.0f * M_PI / 180.0f; // 270 degrees in radians
        const float startAngle = -135.0f * M_PI / 180.0f; // Start at -135 degrees
        const float currentAngle = startAngle + (currentValue * angleRange);
        const float physicalAngle = startAngle + (physicalValue * angleRange);
        
        float centerX = static_cast<float>(x);
        float centerY = static_cast<float>(y);
        
        // Draw track background (dark gray arc)
        canvas->set_color(canvas_ity::stroke_style, 0.2f, 0.2f, 0.2f, 1.0f);
        canvas->set_line_width(trackWidth);
        canvas->line_cap = canvas_ity::circle;
        canvas->begin_path();
        canvas->arc(centerX, centerY, trackRadius, startAngle, startAngle + angleRange, false);
        canvas->stroke();
        
        // Draw current value track (bright arc up to currentValue)
        if (currentValue > 0.0f) {
            canvas->set_color(canvas_ity::stroke_style, 0.3f, 0.7f, 1.0f, 1.0f); // Light blue
            canvas->set_line_width(trackWidth);
            canvas->begin_path();
            canvas->arc(centerX, centerY, trackRadius, startAngle, currentAngle, false);
            canvas->stroke();
        }
        
        // Draw knob body with gradient effect using circles
        // Outer shadow/border
        canvas->set_color(canvas_ity::fill_style, 0.1f, 0.1f, 0.1f, 1.0f);
        canvas->begin_path();
        canvas->arc(centerX, centerY, knobRadius + 1.0f, 0, 2.0f * M_PI, false);
        canvas->fill();
        
        // Main knob body
        canvas->set_color(canvas_ity::fill_style, 0.45f, 0.45f, 0.45f, 1.0f);
        canvas->begin_path();
        canvas->arc(centerX, centerY, knobRadius, 0, 2.0f * M_PI, false);
        canvas->fill();
        
        // Inner highlight (top-left light)
        canvas->set_color(canvas_ity::fill_style, 0.6f, 0.6f, 0.6f, 1.0f);
        canvas->begin_path();
        canvas->arc(centerX - 3.0f, centerY - 3.0f, knobRadius * 0.7f, 0, 2.0f * M_PI, false);
        canvas->fill();
        
        // Center area
        canvas->set_color(canvas_ity::fill_style, 0.5f, 0.5f, 0.5f, 1.0f);
        canvas->begin_path();
        canvas->arc(centerX, centerY, knobRadius * 0.8f, 0, 2.0f * M_PI, false);
        canvas->fill();
        
        // Calculate white dot position based on physical value
        float dotX = centerX + cos(physicalAngle) * dotDistance;
        float dotY = centerY + sin(physicalAngle) * dotDistance;
        
        // Draw white position dot
        canvas->set_color(canvas_ity::fill_style, 1.0f, 1.0f, 1.0f, 1.0f);
        canvas->begin_path();
        canvas->arc(dotX, dotY, dotRadius, 0, 2.0f * M_PI, false);
        canvas->fill();
        
        // Draw text on knob (center)
        if (!textOnKnob.empty()) {
            canvas->set_color(canvas_ity::fill_style, 1.0f, 1.0f, 1.0f, 1.0f);
            canvas->text_align = canvas_ity::center;
            canvas->text_baseline = canvas_ity::middle;
            canvas->fill_text(textOnKnob.c_str(), centerX, centerY);
        }
        
        // Draw text below knob
        if (!textBelowKnob.empty()) {
            canvas->set_color(canvas_ity::fill_style, 1.0f, 1.0f, 1.0f, 1.0f);
            canvas->text_align = canvas_ity::center;
            canvas->text_baseline = canvas_ity::top;
            canvas->fill_text(textBelowKnob.c_str(), centerX, centerY + knobRadius + 8.0f);
        }
    }
    
private:
    void drawEncoders() {
        if (!parentUI) return;
        
        // Calculate knob positions - 8 knobs evenly spaced across the display
        const float knobSpacing = static_cast<float>(DISPLAY_WIDTH) / 8.0f;
        const float knobY = static_cast<float>(DISPLAY_HEIGHT) / 2.0f; // Center vertically
        
        for (int i = 0; i < 8; i++) {
            float knobX = knobSpacing * (i + 0.5f); // Center each knob in its section
            float currentValue = 0.5f; // TODO: Get actual parameter value from Resolume
            float physicalValue = parentUI->getEncoderPosition(i);
            
            // Generate knob labels
            std::string knobText = std::to_string(i + 1); // Simple numbering for now
            std::string belowText = "Track " + std::to_string(i + 1);
            
            drawKnob(static_cast<int>(knobX), static_cast<int>(knobY), 
                    knobText, belowText, currentValue, physicalValue);
        }
    }

};