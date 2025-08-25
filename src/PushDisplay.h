
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
        
        // Fill entire screen with green for basic test
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        nvgFillColor(vg, nvgRGB(0, 255, 0)); // Bright green
        nvgFill(vg);
        
        // TODO: Add more sophisticated UI rendering based on parentUI state
        // if (parentUI) {
        //     // Read state from parentUI and render accordingly
        // }
        
        // End NanoVG frame
        nvgEndFrame(vg);
        
        // Unbind framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        
        std::cout << "Update completed - green screen rendered" << std::endl;
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
        
        // Check if we have green pixels
        bool hasGreen = false;
        for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT * 4; i += 4) {
            if (displayBuffer[i+1] > 200) { // Check green channel
                hasGreen = true;
                break;
            }
        }
        std::cout << "Frame has green content: " << (hasGreen ? "YES" : "NO") << std::endl;
        
        // Send to Push device
        if (pushDevice.isDeviceConnected()) {
            pushDevice.sendDisplayFrameBlocking(displayBuffer);
        } else {
            std::cerr << "Push device not connected" << std::endl;
        }
    }
};

