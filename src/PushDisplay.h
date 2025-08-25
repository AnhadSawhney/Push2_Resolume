#pragma once

#include "PushUSB.h"
// Forward declaration to avoid circular dependency
class PushUI;

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

// Display constants
static const int DISPLAY_WIDTH = 960;
static const int DISPLAY_HEIGHT = 160;

class PushDisplay {
private:
    PushUSB& pushDevice;
    PushUI* parentUI;  // Read-only reference to PushUI
    NVGcontext* vg;
    GLFWwindow* window;
    GLuint fbo, colorTexture;
    uint8_t displayBuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT * 4]; // RGBA
    
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
        if (!parentUI) return;

        const float spacing = static_cast<float>(DISPLAY_WIDTH) / 8.0f;
        const float y = static_cast<float>(DISPLAY_HEIGHT) * 0.5f;

        for (int i = 0; i < 8; ++i) {
            float x = spacing * (i + 0.5f);
            float currentValue = 0.5f; // placeholder
            float physicalValue = parentUI->getEncoderPosition(i);

            std::string knobText = std::to_string(i + 1);
            std::string belowText = "Track " + std::to_string(i + 1);

            drawKnob(static_cast<int>(x), static_cast<int>(y),
                     knobText, belowText, currentValue, physicalValue);
        }
    }

public:
    PushDisplay(PushUSB& push, PushUI* ui = nullptr) : pushDevice(push), parentUI(ui), vg(nullptr), window(nullptr), fbo(0), colorTexture(0) {
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
        
        // Create simple framebuffer
        glGenFramebuffers(1, &fbo);
        glGenTextures(1, &colorTexture);
        
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
        
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "Framebuffer not complete!" << std::endl;
        } else {
            std::cout << "Framebuffer created successfully" << std::endl;
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
        if (!vg || !window || !fbo) {
            std::cerr << "update(): Components not initialized" << std::endl;
            return;
        }
        
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

        drawEncoders();
        
        // End NanoVG frame
        nvgEndFrame(vg);
        
        // Unbind framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    
    void sendToDevice() {
        if (!vg || !window || !fbo) {
            std::cerr << "sendToDevice(): Components not initialized" << std::endl;
            return;
        }
        
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
        
        // Read pixels
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, displayBuffer);
        
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            std::cerr << "OpenGL error during glReadPixels: " << error << std::endl;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            return;
        }
        
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        
        // Flip image vertically (OpenGL is bottom-up, device expects top-down)
        uint8_t tempBuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT * 4];
        for (int y = 0; y < DISPLAY_HEIGHT; y++) {
            int srcY = DISPLAY_HEIGHT - 1 - y;
            memcpy(&tempBuffer[y * DISPLAY_WIDTH * 4], 
                   &displayBuffer[srcY * DISPLAY_WIDTH * 4], 
                   DISPLAY_WIDTH * 4);
        }
        memcpy(displayBuffer, tempBuffer, sizeof(displayBuffer));
        
        // Send to Push device
        if (pushDevice.isDeviceConnected()) {
            pushDevice.sendDisplayFrameBlocking(displayBuffer);
        } else {
            std::cerr << "Push device not connected" << std::endl;
        }
    }
    
    void drawKnob(int x, int y, const std::string& textOnKnob, const std::string& textBelowKnob,
                  float currentValue, float physicalValue)
    {
        if (!vg || !window) return;

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
        
        // Draw track background (dark gray arc)
        nvgStrokeColor(vg, nvgRGBA(51, 51, 51, 255)); // 0.2f * 255
        nvgStrokeWidth(vg, trackWidth);
        nvgLineCap(vg, NVG_ROUND);
        nvgBeginPath(vg);
        nvgArc(vg, centerX, centerY, trackRadius, startAngle, startAngle + angleRange, NVG_CW);
        nvgStroke(vg);
        
        // Draw current value track (bright arc up to currentValue)
        if (currentValue > 0.0f) {
            nvgStrokeColor(vg, nvgRGBA(77, 179, 255, 255)); // Light blue
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
        nvgFillColor(vg, nvgRGBA(26, 26, 26, 255)); // 0.1f * 255
        nvgFill(vg);
        
        // Main knob body with radial gradient
        NVGpaint knobGradient = nvgRadialGradient(vg, centerX - 8.0f, centerY - 8.0f, 0.0f, knobRadius,
                                                  nvgRGBA(153, 153, 153, 255), // 0.6f * 255 - highlight
                                                  nvgRGBA(115, 115, 115, 255)); // 0.45f * 255 - base
        nvgBeginPath(vg);
        nvgCircle(vg, centerX, centerY, knobRadius);
        nvgFillPaint(vg, knobGradient);
        nvgFill(vg);
        
        // Inner highlight (top-left light)
        nvgBeginPath(vg);
        nvgCircle(vg, centerX - 3.0f, centerY - 3.0f, knobRadius * 0.7f);
        nvgFillColor(vg, nvgRGBA(153, 153, 153, 255)); // 0.6f * 255
        nvgFill(vg);
        
        // Center area
        nvgBeginPath(vg);
        nvgCircle(vg, centerX, centerY, knobRadius * 0.8f);
        nvgFillColor(vg, nvgRGBA(128, 128, 128, 255)); // 0.5f * 255
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
        if (!textOnKnob.empty()) {
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(vg, centerX, centerY, textOnKnob.c_str(), nullptr);
        }
        
        // Draw text below knob
        if (!textBelowKnob.empty()) {
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
            nvgText(vg, centerX, centerY + knobRadius + 8.0f, textBelowKnob.c_str(), nullptr);
        }
    }
};

