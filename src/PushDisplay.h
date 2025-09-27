#pragma once

#include "PushUSB.h"
// Forward declaration to avoid circular dependency
class PushUI;
struct Encoder;

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "nanovg.h"
#include "nanovg_gl.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <cmath>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846264338327f
#endif

#ifndef GL_RGB565
#define GL_RGB565 0x8D62
#endif

// Performance timing
//#define STOPWATCH

#ifdef STOPWATCH
#include <chrono>
#define TIMER_START(name) auto timer_##name = std::chrono::high_resolution_clock::now()
#define TIMER_END(name) do { \
    auto timer_##name##_end = std::chrono::high_resolution_clock::now(); \
    auto timer_##name##_duration = std::chrono::duration_cast<std::chrono::microseconds>(timer_##name##_end - timer_##name).count(); \
    std::cout << "TIMER [" << #name << "]: " << timer_##name##_duration << " us" << std::endl; \
} while(0)
#else
#define TIMER_START(name)
#define TIMER_END(name)
#endif

// Display constants
#ifndef DISPLAY_WIDTH
    #define DISPLAY_WIDTH 960
#endif

#ifndef DISPLAY_HEIGHT
    #define DISPLAY_HEIGHT 160
#endif

class PushDisplay {
private:
    PushUSB& pushDevice;
    PushUI* parentUI;  // Read-only reference to PushUI
    NVGcontext* vg;
    GLFWwindow* window;
    GLuint fbo, colorTexture;
    uint16_t displayBuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT]; // RGB565
    int fontRegular; // Add font handle
    
    // Helper to ensure OpenGL context and GLAD are properly initialized for the current thread
    bool ensureGLContext() {
        if (!window) {
            std::cerr << "ensureGLContext(): No window available" << std::endl;
            return false;
        }
        
        // Make the context current for this thread
        glfwMakeContextCurrent(window);
        
        // Ensure GLAD is loaded for this thread
        if (!gladLoadGL(glfwGetProcAddress)) {
            std::cerr << "ensureGLContext(): Failed to load OpenGL with GLAD for this thread" << std::endl;
            return false;
        }
        
        return true;
    }

    void drawEncoders() {
        TIMER_START(encoders_draw);
        if (!parentUI) return;

        const float spacing = DISPLAY_WIDTH / 8.0f;
        const float y = DISPLAY_HEIGHT * 0.5f;

        // Update virtual values from Resolume tracker
        parentUI->updateEncoderVirtualValues();

        for (int i = 0; i < 8; ++i) {
            TIMER_START(one_encoder);
            float x = spacing * (i + 0.5f);
            Encoder encoder = parentUI->getEncoder(i);

            std::string knobText = std::to_string(i + 1);

            drawKnob(static_cast<int>(x), static_cast<int>(y),
                     knobText, encoder.displayName, encoder.virtualValue, encoder.physicalValue);
            TIMER_END(one_encoder);
        }
        TIMER_END(encoders_draw);
    }

public:
    PushDisplay(PushUSB& push, PushUI* ui = nullptr) : pushDevice(push), parentUI(ui), vg(nullptr), window(nullptr), fbo(0), colorTexture(0), fontRegular(-1) {
        // Initialize GLFW
        if (!glfwInit()) {
            std::cerr << "GLFW init failed" << std::endl;
            return;
        }
        
        // Create a minimal hidden window
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        
        window = glfwCreateWindow(1, 1, "offscreen", nullptr, nullptr);
        if (!window) {
            std::cerr << "Failed to create GLFW window" << std::endl;
            glfwTerminate();
            return;
        }
        
        glfwMakeContextCurrent(window);
        
        // Load OpenGL
        if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
            std::cerr << "Failed to load OpenGL" << std::endl;
            glfwDestroyWindow(window);
            glfwTerminate();
            return;
        }
        
        std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
        
        // Create NanoVG context
        vg = nvgCreateGL3(NVG_ANTIALIAS);
        if (!vg) {
            std::cerr << "Failed to create NanoVG context" << std::endl;
            glfwDestroyWindow(window);
            glfwTerminate();
            return;
        }
        
        std::cout << "NanoVG context created successfully" << std::endl;
        
        // Load a default font - you can use NanoVG's built-in font creation or load a TTF file
        // Option 1: Load a TTF font file (recommended)
        // Replace "path/to/font.ttf" with the actual path to your font file if available
        fontRegular = nvgCreateFont(vg, "regular", "C:/Windows/Fonts/arial.ttf");

        // Fallback: create a simple font if all else fails
        if (fontRegular == -1) {
            std::cout << "Warning: Could not load font, text may not render properly" << std::endl;
            // You could create a minimal bitmap font here as fallback
        } else {
            std::cout << "Font loaded successfully" << std::endl;
        }
        
        // Create framebuffer with RGB565 color attachment
        glGenFramebuffers(1, &fbo);
        glGenTextures(1, &colorTexture);
        
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB565, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
        
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "Framebuffer not complete!" << std::endl;
        } else {
            std::cout << "RGB565 Framebuffer created successfully" << std::endl;
        }
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        
        // Clear display buffer
        memset(displayBuffer, 0, sizeof(displayBuffer));
    }
    
    ~PushDisplay() {
        if (window) {
            // Ensure context is current for cleanup
            ensureGLContext();
            if (vg) {
                nvgDeleteGL3(vg);
            }
            if (fbo) {
                glDeleteFramebuffers(1, &fbo);
            }
            if (colorTexture) {
                glDeleteTextures(1, &colorTexture);
            }
            glfwDestroyWindow(window);
        }
        glfwTerminate();
    }
    
    // Set the PushUI reference after construction
    void setParentUI(PushUI* parent) { parentUI = parent; }
    
    void update() {
        TIMER_START(total_update);
        
        if (!vg || !window || !fbo) {
            std::cerr << "update(): Components not initialized" << std::endl;
            return;
        }
        
        TIMER_START(gl_setup);
        // Ensure OpenGL context and GLAD are properly set up for this thread
        if (!ensureGLContext()) {
            std::cerr << "update(): Failed to ensure GL context" << std::endl;
            return;
        }
        
        // Bind framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        
        // Clear to black
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Start NanoVG frame
        nvgBeginFrame(vg, DISPLAY_WIDTH, DISPLAY_HEIGHT, 1.0f);
        TIMER_END(gl_setup);
        
        TIMER_START(background_draw);
        // Background
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        nvgFillColor(vg, nvgRGB(0, 0, 0));
        nvgFill(vg);

        if (parentUI && parentUI->getMode() == PushUI::Mode::Selecting) {
            nvgStrokeColor(vg, nvgRGB(0, 255, 0));
            nvgStrokeWidth(vg, 2.0f);
            nvgBeginPath(vg);
            nvgRect(vg, 1.0f, 1.0f, (float)DISPLAY_WIDTH - 2.0f, (float)DISPLAY_HEIGHT - 2.0f);
            nvgStroke(vg);
        }
        TIMER_END(background_draw);

        drawEncoders();
        
        TIMER_START(nvg_end);
        // End NanoVG frame
        nvgEndFrame(vg);
        
        // Unbind framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        TIMER_END(nvg_end);
        
        TIMER_END(total_update);
    }
    
    void sendToDevice() {
        TIMER_START(total_send);
        
        if (!vg || !window || !fbo) {
            std::cerr << "sendToDevice(): Components not initialized" << std::endl;
            return;
        }
        
        TIMER_START(gl_readback_setup);
        // Ensure OpenGL context and GLAD are properly set up for this thread
        if (!ensureGLContext()) {
            std::cerr << "sendToDevice(): Failed to ensure GL context" << std::endl;
            return;
        }
        
        // Bind framebuffer for reading
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        
        // Check framebuffer status
        GLenum status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "Read framebuffer not complete: " << status << std::endl;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            return;
        }
        TIMER_END(gl_readback_setup);
        
        TIMER_START(pixel_readback);
        // Read RGB565 pixels directly
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, displayBuffer);
        
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            std::cerr << "OpenGL error during glReadPixels: " << error << std::endl;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            return;
        }
        
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        TIMER_END(pixel_readback);
        
        TIMER_START(image_flip);
        // Flip image vertically (OpenGL is bottom-up, device expects top-down)
        uint16_t tempBuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT];
        for (int y = 0; y < DISPLAY_HEIGHT; y++) {
            int srcY = DISPLAY_HEIGHT - 1 - y;
            memcpy(&tempBuffer[y * DISPLAY_WIDTH], 
                   &displayBuffer[srcY * DISPLAY_WIDTH], 
                   DISPLAY_WIDTH * sizeof(uint16_t));
        }
        memcpy(displayBuffer, tempBuffer, sizeof(displayBuffer));
        TIMER_END(image_flip);
        
        TIMER_START(usb_transfer);
        // Queue frame for non-blocking USB transfer via USB thread
        if (pushDevice.isDeviceConnected()) {
            pushDevice.sendDisplayFrame565(reinterpret_cast<uint8_t*>(displayBuffer));
        } else {
            std::cerr << "Push device not connected" << std::endl;
        }
        TIMER_END(usb_transfer);
        
        TIMER_END(total_send);
    }
    
    void drawKnob(int x, int y, const std::string& textOnKnob, const std::string& textBelowKnob,
                  float currentValue, float physicalValue)
    {
        if (!vg || !window) return;

        // Set font and size before drawing text
        if (fontRegular != -1) {
            nvgFontFace(vg, "regular");
            nvgFontSize(vg, 20.0f); // Adjust size as needed
        }

        const float knobRadius = 25.0f;
        const float trackRadius = 32.0f;
        const float trackWidth = 4.0f;
        const float dotRadius = 3.0f;
        const float dotDistance = 18.0f;
        
        // Clamp values to 0-1 range
        currentValue = std::max(0.0f, std::min(1.0f, currentValue));
        physicalValue = std::max(0.0f, std::min(1.0f, physicalValue));
        
        // Convert values to angles (270 degrees rotation range: -225° to +45°)
        const float angleRange = 270.0f * M_PI / 180.0f; // 270 degrees in radians
        const float startAngle = -225.0f * M_PI / 180.0f; // Start at -225 degrees
        const float currentAngle = startAngle + (currentValue * angleRange);
        const float physicalAngle = startAngle + (physicalValue * angleRange);
        
        float centerX = static_cast<float>(x);
        float centerY = static_cast<float>(y);
        
        // Draw track background (dark gray arc) - RGB565 compatible
        nvgStrokeColor(vg, nvgRGBA(51, 51, 51, 255));
        nvgStrokeWidth(vg, trackWidth);
        nvgLineCap(vg, NVG_ROUND);
        nvgBeginPath(vg);
        nvgArc(vg, centerX, centerY, trackRadius, startAngle, startAngle + angleRange, NVG_CW);
        nvgStroke(vg);
        
        // Draw current value track (light blue) - swap R and B for RGB565
        if (currentValue > 0.0f) {
            nvgStrokeColor(vg, nvgRGBA(255, 179, 77, 255)); // Swapped: was (77, 179, 255)
            nvgStrokeWidth(vg, trackWidth);
            nvgLineCap(vg, NVG_ROUND);
            nvgBeginPath(vg);
            nvgArc(vg, centerX, centerY, trackRadius, startAngle, currentAngle, NVG_CW);
            nvgStroke(vg);
        }
        
        // Draw knob body with gradient effect
        
        // Outer shadow/border
        nvgBeginPath(vg);
        nvgCircle(vg, centerX, centerY, knobRadius + 1.0f);
        nvgFillColor(vg, nvgRGBA(26, 26, 26, 255));
        nvgFill(vg);
        
        // Main knob body with radial gradient
        NVGpaint knobGradient = nvgRadialGradient(vg, centerX - 8.0f, centerY - 8.0f, 0.0f, knobRadius,
                                                  nvgRGBA(153, 153, 153, 255), // highlight
                                                  nvgRGBA(115, 115, 115, 255)); // base
        nvgBeginPath(vg);
        nvgCircle(vg, centerX, centerY, knobRadius);
        nvgFillPaint(vg, knobGradient);
        nvgFill(vg);
        
        // Inner highlight (top-left light)
        nvgBeginPath(vg);
        nvgCircle(vg, centerX - 3.0f, centerY - 3.0f, knobRadius * 0.7f);
        nvgFillColor(vg, nvgRGBA(153, 153, 153, 255));
        nvgFill(vg);
        
        // Center area
        nvgBeginPath(vg);
        nvgCircle(vg, centerX, centerY, knobRadius * 0.8f);
        nvgFillColor(vg, nvgRGBA(128, 128, 128, 255));
        nvgFill(vg);
        
        // Calculate white dot position based on physical value
        float dotX = centerX + cosf(physicalAngle) * dotDistance;
        float dotY = centerY + sinf(physicalAngle) * dotDistance;
        
        // Draw white position dot
        nvgBeginPath(vg);
        nvgCircle(vg, dotX, dotY, dotRadius);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
        nvgFill(vg);
        
        // Draw text on knob (center)
        if (!textOnKnob.empty() && fontRegular != -1) {
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFontSize(vg, 10.0f); // Smaller text for knob center
            nvgText(vg, centerX, centerY, textOnKnob.c_str(), nullptr);
        }
        
        // Draw text below knob
        if (!textBelowKnob.empty() && fontRegular != -1) {
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
            nvgFontSize(vg, 8.0f); // Even smaller text for labels
            nvgText(vg, centerX, centerY + knobRadius + 8.0f, textBelowKnob.c_str(), nullptr);
        }
    }
};

