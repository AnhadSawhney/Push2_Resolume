#pragma once

#include "RtMidi.h"
#include <vector>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <iostream>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <queue>
#include <condition_variable>

#define NOMINMAX
#include "libusb.h"

#define ABLETON_VENDOR_ID 0x2982
#define PUSH2_PRODUCT_ID  0x1967

// Rename to avoid conflict with RtMidi's MidiMessage
struct PushMidiMessage {
    std::vector<uint8_t> data;
    
    PushMidiMessage(const std::vector<uint8_t>& msg) : data(msg) {}
    PushMidiMessage(const uint8_t* rawData, size_t size) : data(rawData, rawData + size) {}
    
    bool isNoteOn() const { 
        return !data.empty() && (data[0] & 0xF0) == 0x90 && data.size() >= 3 && data[2] > 0; 
    }
    bool isNoteOff() const { 
        return !data.empty() && ((data[0] & 0xF0) == 0x80 || 
               ((data[0] & 0xF0) == 0x90 && data.size() >= 3 && data[2] == 0)); 
    }
    bool isControlChange() const { 
        return !data.empty() && (data[0] & 0xF0) == 0xB0; 
    }
    bool isPitchBend() const {
        return !data.empty() && (data[0] & 0xF0) == 0xE0;
    }
    
    uint8_t getNote() const { return (data.size() >= 2) ? data[1] : 0; }
    uint8_t getVelocity() const { return (data.size() >= 3) ? data[2] : 0; }
    uint8_t getController() const { return (data.size() >= 2) ? data[1] : 0; }
    uint8_t getValue() const { return (data.size() >= 3) ? data[2] : 0; }
    uint16_t getPitchBend() const {
        if (data.size() >= 3) {
            // Combine LSB and MSB to get 14-bit pitch bend value
            return static_cast<uint16_t>(data[1]) | (static_cast<uint16_t>(data[2]) << 7);
        }
        return 8192; // Center value
    }
};

// Display constants
static const int DISPLAY_WIDTH = 960;
static const int DISPLAY_HEIGHT = 160;

class PushUSB {
private:
    // RtMidi objects
    std::unique_ptr<RtMidiIn> midiIn;
    std::unique_ptr<RtMidiOut> midiOut;
    
    std::atomic<bool> isConnected;
    std::function<void(const PushMidiMessage&)> midiCallback;  // Updated type

    libusb_device_handle* deviceHandle = nullptr;
    
    // Asynchronous USB transfer management
    libusb_transfer* currentTransfer = nullptr;
    std::atomic<bool> transferInProgress;
    std::mutex transferMutex;
    // Static callback for asynchronous USB transfers
    static void LIBUSB_CALL transferCallback(libusb_transfer* transfer) {
        PushUSB* pushUSB = static_cast<PushUSB*>(transfer->user_data);
        if (pushUSB) {
            pushUSB->onTransferComplete(transfer);
        }
    }
    
    // Handle transfer completion
    void onTransferComplete(libusb_transfer* transfer) {
        std::lock_guard<std::mutex> lock(transferMutex);
        
        if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
            std::cerr << "USB transfer failed with status: " << transfer->status << std::endl;
        }
        
        // Clean up the transfer buffer that was allocated in sendDisplayFrameBlocking565
        if (transfer->buffer) {
            delete[] transfer->buffer;
        }
        
        // Free the transfer and mark as not in progress
        libusb_free_transfer(transfer);
        if (currentTransfer == transfer) {
            currentTransfer = nullptr;
        }
        transferInProgress.store(false);
    }
    
    // Static callback for RtMidi (C-style callback required)
    // This converts RtMidi callbacks to our callback system
    static void midiInputCallback(double timeStamp, std::vector<unsigned char>* message, void* userData) {
        PushUSB* pushUSB = static_cast<PushUSB*>(userData);
        if (pushUSB && pushUSB->midiCallback && !message->empty()) {
            // Convert RtMidi message to our PushMidiMessage
            pushUSB->midiCallback(PushMidiMessage(*message));
        }
    }
    
    // Find Push 2 MIDI ports
    bool findPush2MidiPorts(unsigned int& inputPort, unsigned int& outputPort) {
        // Check input ports
        bool foundInput = false, foundOutput = false;
        
        unsigned int inputPortCount = midiIn->getPortCount();
        for (unsigned int i = 0; i < inputPortCount; i++) {
            std::string portName = midiIn->getPortName(i);
            if (portName.find("Push 2") != std::string::npos || 
                portName.find("Ableton Push 2") != std::string::npos) {
                inputPort = i;
                foundInput = true;
                std::cout << "Found Push 2 input port: " << portName << std::endl;
                break;
            }
        }
        
        unsigned int outputPortCount = midiOut->getPortCount();
        for (unsigned int i = 0; i < outputPortCount; i++) {
            std::string portName = midiOut->getPortName(i);
            if (portName.find("Push 2") != std::string::npos ||
                portName.find("Ableton Push 2") != std::string::npos) {
                outputPort = i;
                foundOutput = true;
                std::cout << "Found Push 2 output port: " << portName << std::endl;
                break;
            }
        }
        
        return foundInput && foundOutput;
    }

    static libusb_device_handle* open_push2_device(){
        int result;

        if ((result = libusb_init(NULL)) < 0) {
            std::cout << "error: [" << result << "] could not initilialize usblib" << std::endl;
            return NULL;
        }

        libusb_set_debug(NULL, LIBUSB_LOG_LEVEL_ERROR);

        libusb_device** devices;
        ssize_t count;
        count = libusb_get_device_list(NULL, &devices);
        if (count < 0) {
            std::cout << "error: [" << count << "] could not get usb device list" << std::endl;
            return NULL;
        }

        libusb_device* device;
        libusb_device_handle* device_handle = NULL;

        std::string ErrorMsg;

        // set message in case we get to the end of the list w/o finding a device
        ErrorMsg = "error: Ableton Push 2 device not found";

        for (int i = 0; (device = devices[i]) != NULL; i++) {
            struct libusb_device_descriptor descriptor;
            if ((result = libusb_get_device_descriptor(device, &descriptor)) < 0) {
                ErrorMsg = "error: [" + std::to_string(result) + "] could not get usb device descriptor";
                continue;
            }

            if (descriptor.bDeviceClass == LIBUSB_CLASS_PER_INTERFACE && descriptor.idVendor == ABLETON_VENDOR_ID && descriptor.idProduct == PUSH2_PRODUCT_ID) {
                if ((result = libusb_open(device, &device_handle)) < 0) {
                    ErrorMsg = "error: [" + std::to_string(result) + "] could not open Ableton Push 2 device";
                } else if ((result = libusb_claim_interface(device_handle, 0)) < 0) {
                    ErrorMsg = "error: [" + std::to_string(result) + "] could not claim interface 0 of Push 2 device";
                    libusb_close(device_handle);
                    device_handle = NULL;
                } else {
                    break; // successfully opened
                }
            }
        }

        if (device_handle == NULL) {
            std::cout << ErrorMsg << std::endl;
        }

        libusb_free_device_list(devices, 1);
        return device_handle;
    }

    static void close_push2_device(libusb_device_handle* device_handle) {
        libusb_release_interface(device_handle, 0);
        libusb_close(device_handle);
    }
    
public:
    PushUSB() : isConnected(false), transferInProgress(false) {
        try {
            midiIn = std::make_unique<RtMidiIn>();
            midiOut = std::make_unique<RtMidiOut>();
        } catch (RtMidiError& error) {
            std::cerr << "RtMidi initialization error: " << error.getMessage() << std::endl;
        }
    }
    
    ~PushUSB() {
        disconnect();
    }
    
    bool initialize() {
        return midiIn && midiOut;
    }
    
    bool connect() {
        if (isConnected.load()) {
            return true;
        }
        
        if (!midiIn || !midiOut) {
            std::cerr << "MIDI objects not initialized" << std::endl;
            return false;
        }
        
        try {
            unsigned int inputPort, outputPort;
            if (!findPush2MidiPorts(inputPort, outputPort)) {
                std::cerr << "Could not find Push 2 MIDI ports" << std::endl;
                std::cout << "\nAvailable MIDI Input Ports:" << std::endl;
                for (unsigned int i = 0; i < midiIn->getPortCount(); i++) {
                    std::cout << "  " << i << ": " << midiIn->getPortName(i) << std::endl;
                }
                std::cout << "\nAvailable MIDI Output Ports:" << std::endl;
                for (unsigned int i = 0; i < midiOut->getPortCount(); i++) {
                    std::cout << "  " << i << ": " << midiOut->getPortName(i) << std::endl;
                }
                return false;
            }
            
            // Open MIDI ports
            midiIn->openPort(inputPort);
            midiOut->openPort(outputPort);
            
            // Set up input callback - THIS IS IMPORTANT FOR RECEIVING INPUT
            midiIn->setCallback(&midiInputCallback, this);
            midiIn->ignoreTypes(false, false, false); // Don't ignore any message types
            
            isConnected.store(true);
            std::cout << "Successfully connected to Push 2 MIDI ports" << std::endl;
            
            // Test the connection
            clearAllPads();            
        } catch (RtMidiError& error) {
            std::cerr << "MIDI connection error: " << error.getMessage() << std::endl;
            return false;
        }

        std::cout << "Opening Push 2 USB display..." << std::endl;
        deviceHandle = open_push2_device();

        if (!deviceHandle) {
            std::cerr << "Failed to open Push 2 USB display" << std::endl;
            return false;
        }

        std::cout << "Push 2 USB display opened successfully" << std::endl;

        return true;
    }
    
    void disconnect() {
        if (!isConnected.load()) {
            return;
        }
        
        try {
            if (midiIn && midiIn->isPortOpen()) {
                midiIn->closePort();
            }
            if (midiOut && midiOut->isPortOpen()) {
                midiOut->closePort();
            }
            
            isConnected.store(false);
            std::cout << "Disconnected from Push 2 MIDI" << std::endl;
            
        } catch (RtMidiError& error) {
            std::cerr << "MIDI disconnect error: " << error.getMessage() << std::endl;
        }

        // Cancel any pending USB transfers
        {
            std::lock_guard<std::mutex> lock(transferMutex);
            if (currentTransfer) {
                libusb_cancel_transfer(currentTransfer);
                // Transfer will be freed in the callback
                currentTransfer = nullptr;
            }
        }

        if (deviceHandle) {
            std::cout << "Closing Push 2 USB display..." << std::endl;
            close_push2_device(deviceHandle);
            deviceHandle = nullptr;
        }
    }
    
    bool isDeviceConnected() const { 
        return isConnected.load(); 
    }
    
    // Set callback for receiving MIDI input from Push 2
    void setMidiCallback(std::function<void(const PushMidiMessage&)> callback) {
        midiCallback = callback;
    }
    
    // Send raw MIDI message
    bool sendMidiMessage(const std::vector<uint8_t>& message) {
        if (!isConnected.load() || !midiOut || !midiOut->isPortOpen()) {
            return false;
        }
        
        try {
            midiOut->sendMessage(&message);
            return true;
        } catch (RtMidiError& error) {
            std::cerr << "MIDI send error: " << error.getMessage() << std::endl;
            return false;
        }
    }
    
    // Send SysEx message
    bool sendSysEx(const std::vector<uint8_t>& sysex) {
        return sendMidiMessage(sysex);
    }

    void reapplyPalette() {
        std::vector<uint8_t> sysex = {
            0xF0, 0x00, 0x21, 0x1D, 0x01, 0x01, 0x05, 0xF7
        };
        sendSysEx(sysex);
    }

    // Send Push 2 palette sysex command
    void setPaletteEntry(uint8_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {        
        // Split each color into LSB (7 bits) and MSB (1 bit)
        auto split = [](uint8_t v) -> std::pair<uint8_t, uint8_t> {
            return { static_cast<uint8_t>(v & 0x7F), static_cast<uint8_t>((v >> 7) & 0x01) };
        };
        auto [r_lsb, r_msb] = split(r);
        auto [g_lsb, g_msb] = split(g);
        auto [b_lsb, b_msb] = split(b);
        auto [w_lsb, w_msb] = split(w);

        std::vector<uint8_t> sysex = {
            0xF0, 0x00, 0x21, 0x1D, 0x01, 0x01, 0x03, // header + command
            index,
            r_lsb, r_msb,
            g_lsb, g_msb,
            b_lsb, b_msb,
            w_lsb, w_msb,
            0xF7
        };
        sendSysEx(sysex);
        reapplyPalette();
    }
    
    bool setPadColorIndex(int padNumber, uint8_t colorIndex) {
        if (!isConnected.load() || padNumber < 36 || padNumber > 99) {
            return false;
        }

        return sendMidiMessage({0x90, static_cast<uint8_t>(padNumber), colorIndex});
    }

    // Set button color (control change)
    bool setButtonColorIndex(int buttonNumber, uint8_t colorIndex) {
        if (!isConnected.load()) {
            return false;
        }

        return sendMidiMessage({0xB0, static_cast<uint8_t>(buttonNumber), colorIndex});
    }
    
    // Clear all pads
    bool clearAllPads() {
        bool success = true;
        for (int note = 36; note <= 99; note++) {
            success &= sendMidiMessage({0x90, static_cast<uint8_t>(note), 0x00});
        }
        return success;
    }

    // Get palette entry from Push 2 (blocking, returns true if reply received)
    bool getPaletteEntry(uint8_t index, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& w) {
        // Send sysex request: F0 00 21 1D 01 01 04 00 <index> F7
        std::vector<uint8_t> sysex = {
            0xF0, 0x00, 0x21, 0x1D, 0x01, 0x01, 0x04, 0x00, index, 0xF7
        };

        // We'll need to capture the reply. This is a blocking call and not thread safe.
        // In a real implementation, this should be async/event-driven.
        std::atomic<bool> gotReply{false};
        uint8_t replyR = 0, replyG = 0, replyB = 0, replyW = 0;

        auto oldCallback = midiCallback;
        midiCallback = [&](const PushMidiMessage& msg) {
            // Expect: F0 00 21 1D 01 01 04 00 <index> r g b w F7
            if (msg.data.size() >= 13 &&
                msg.data[0] == 0xF0 && msg.data[1] == 0x00 && msg.data[2] == 0x21 &&
                msg.data[3] == 0x1D && msg.data[4] == 0x01 && msg.data[5] == 0x01 &&
                msg.data[6] == 0x04 && msg.data[7] == 0x00 && msg.data[8] == index &&
                msg.data[12] == 0xF7) {
                replyR = msg.data[9];
                replyG = msg.data[10];
                replyB = msg.data[11];
                replyW = msg.data[12 - 1]; // msg.data[11] is b, msg.data[12] is w
                gotReply = true;
            }
        };

        sendSysEx(sysex);

        // Wait for reply (timeout after 100ms)
        for (int i = 0; i < 100; ++i) {
            if (gotReply) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        midiCallback = oldCallback;

        if (gotReply) {
            r = replyR;
            g = replyG;
            b = replyB;
            w = replyW;
            return true;
        }
        return false;
    }

    // Set just the RGB part of a palette entry (preserve W)
    void setPaletteEntryRGB(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
        uint8_t oldR = 0, oldG = 0, oldB = 0, oldW = 0;
        if (getPaletteEntry(index, oldR, oldG, oldB, oldW)) {
            setPaletteEntry(index, r, g, b, oldW);
        } else {
            setPaletteEntry(index, r, g, b, 0);
        }
    }

    // Set just the BW part of a palette entry (preserve RGB)
    void setPaletteEntryBW(uint8_t index, uint8_t w) {
        uint8_t oldR = 0, oldG = 0, oldB = 0, oldW = 0;
        if (getPaletteEntry(index, oldR, oldG, oldB, oldW)) {
            setPaletteEntry(index, oldR, oldG, oldB, w);
        } else {
            setPaletteEntry(index, 0, 0, 0, w);
        }
    }

    // Set touch strip LEDs (31 LEDs, values 0-7)
    bool setTouchStripLEDs(const uint8_t ledValues[31]) {
        if (!isConnected.load()) {
            return false;
        }

        // Validate LED values are in range 0-7
        for (int i = 0; i < 31; ++i) {
            if (ledValues[i] > 7) {
                return false;
            }
        }

        // Pack LED values according to specification:
        // Each byte packs two 3-bit LED values, with LED 30 getting special treatment
        std::vector<uint8_t> sysex = {
            0xF0, 0x00, 0x21, 0x1D, 0x01, 0x01, 0x19 // header + command ID
        };

        // Pack LEDs 0-29 into 15 bytes (2 LEDs per byte)
        for (int i = 0; i < 15; i++) {
            uint8_t led_low = ledValues[i * 2];     // even index LED
            uint8_t led_high = ledValues[i * 2 + 1]; // odd index LED
            uint8_t packed = (led_high << 3) | led_low;
            sysex.push_back(packed);
        }

        // Pack LED 30 into the last byte (bits 2-0, with bits 7-3 as zero)
        uint8_t last_byte = ledValues[30] & 0x07;
        sysex.push_back(last_byte);

        sysex.push_back(0xF7); // end of sysex

        return sendSysEx(sysex);
    }

    // Configure touch strip for host control via sysex commands
    bool configureTouchStrip() {
        if (!isConnected.load()) {
            return false;
        }

        // Configuration flags:
        // bit 0: LEDs Controlled By (1 = Host)
        // bit 1: Host Sends (1 = Sysex)
        // bit 2: Values Sent As (0 = Pitch Bend)
        // bit 3: LEDs Show (1 = Point)
        // bit 4: Bar Starts At (0 = Bottom)
        // bit 5: Do Autoreturn (0 = No)
        // bit 6: Autoreturn To (0 = Bottom)
        //
        // Setting to 0x0B (00001011) = host control, sysex, pitch bend, point, center, autoreturn, bottom
        uint8_t config = 0x0B;

        std::vector<uint8_t> sysex = {
            0xF0, 0x00, 0x21, 0x1D, 0x01, 0x01, 0x17, // header + command ID
            config,
            0xF7
        };

        return sendSysEx(sysex);
    }

    // Send frame to Push 2 display (RGB565 format) using async libusb
    // Non-blocking version - uses libusb async transfers
    bool sendDisplayFrame565(const uint8_t* rgb565Data) {
        if (!deviceHandle || !rgb565Data) {
            return false;
        }

        // Check if a transfer is already in progress
        if (transferInProgress.load()) {
            // Previous frame still being sent, skip this frame to avoid blocking
            return false;
        }

        std::lock_guard<std::mutex> lock(transferMutex);
        
        // Double-check after acquiring lock
        if (transferInProgress.load()) {
            return false;
        }

        // Allocate transfer structure
        libusb_transfer* transfer = libusb_alloc_transfer(0);
        if (!transfer) {
            std::cerr << "Failed to allocate USB transfer" << std::endl;
            return false;
        }

        // Calculate total transfer size: 16 byte header + (160 lines * 2048 bytes per line)
        const size_t totalSize = 16 + (160 * 2048);
        
        // Allocate buffer for the entire frame (this will be freed in callback)
        uint8_t* transferBuffer = new uint8_t[totalSize];
        
        // Prepare frame header
        uint8_t frameHeader[16] = {
            0xFF, 0xCC, 0xAA, 0x88,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00
        };
        
        // Copy header to transfer buffer
        memcpy(transferBuffer, frameHeader, 16);
        
        // Process RGB565 data line by line and add to transfer buffer
        uint8_t* bufferPtr = transferBuffer + 16;
        for (int y = 0; y < 160; y++) {
            // Apply XOR pattern to RGB565 line directly into transfer buffer
            applyXORPatternToRGB565Line(rgb565Data + (y * 960 * 2), bufferPtr);
            bufferPtr += 2048;
        }

        // Setup async transfer
        libusb_fill_bulk_transfer(
            transfer,                    // transfer
            deviceHandle,               // device handle
            0x01,                       // endpoint
            transferBuffer,             // buffer
            static_cast<int>(totalSize), // length
            transferCallback,           // callback
            this,                       // user data
            5000                        // timeout (5 seconds)
        );

        // Submit the transfer
        int result = libusb_submit_transfer(transfer);
        if (result != 0) {
            std::cerr << "Failed to submit USB transfer: " << libusb_error_name(result) << std::endl;
            libusb_free_transfer(transfer);
            delete[] transferBuffer;
            return false;
        }

        // Mark transfer as in progress
        currentTransfer = transfer;
        transferInProgress.store(true);
        
        return true;
    }

    // Send frame to Push 2 display (legacy RGBA format)
    bool sendDisplayFrameBlocking(const uint8_t* rgbaData) {
        if (!deviceHandle || !rgbaData) {
            return false;
        }

        // Send frame header first
        uint8_t frameHeader[16] = {
            0xFF, 0xCC, 0xAA, 0x88,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00
        };

        int transferred = 0;
        int result = libusb_bulk_transfer(deviceHandle, 0x01, frameHeader, 16, &transferred, 1000);
        if (result != 0 || transferred != 16) {
            std::cerr << "Failed to send frame header: " << result << std::endl;
            return false;
        }

        // Convert and send pixel data line by line
        uint8_t lineBuffer[2048]; // 1920 bytes pixel data + 128 filler bytes

        for (int y = 0; y < 160; y++) {
            // Convert RGBA8 line to RGB565 with XOR pattern
            convertLineToRGB565(rgbaData + (y * 960 * 4), lineBuffer, 960);

            // Send line buffer
            result = libusb_bulk_transfer(deviceHandle, 0x01, lineBuffer, 2048, &transferred, 1000);
            if (result != 0 || transferred != 2048) {
                std::cerr << "Failed to send line " << y << ": " << result << std::endl;
                return false;
            }
        }

        return true;
    }

private:
    // Apply XOR pattern to RGB565 line data
    static inline void applyXORPatternToRGB565Line(const uint8_t* rgb565Line, uint8_t* lineBuffer) {
        // XOR pattern: 0xE7, 0xF3, 0xE7, 0xFF
        const uint32_t xorPattern32 = 0xFFE7F3E7; // Little-endian
        
        // Copy 1920 bytes (960 pixels * 2 bytes) and apply XOR pattern
        const uint32_t* src32 = reinterpret_cast<const uint32_t*>(rgb565Line);
        uint32_t* dst32 = reinterpret_cast<uint32_t*>(lineBuffer);
        
        // Process in 32-bit chunks (1920 bytes = 480 uint32_t)
        for (int i = 0; i < 480; i++) {
            dst32[i] = src32[i] ^ xorPattern32;
        }
        
        // Fill remaining 128 bytes (1920-2048) with XOR pattern
        uint32_t* fillPtr = reinterpret_cast<uint32_t*>(lineBuffer + 1920);
        for (int i = 0; i < 32; i++) {
            fillPtr[i] = xorPattern32;
        }
    }

    // Convert RGBA8 line to RGB565 format with XOR pattern applied (legacy)
    static inline void convertLineToRGB565(const uint8_t* rgbaLine, uint8_t* lineBuffer, int width) {
        // XOR pattern as specified: 0xE7, 0xF3, 0xE7, 0xFF
        const uint32_t xorPattern32 = 0xFFE7F3E7; // Little-endian: E7, F3, E7, FF
        
        // Process pixels in pairs for 32-bit XOR operations
        const int pixelPairs = width / 2;  // 960 / 2 = 480 pairs
        
        const uint8_t* src = rgbaLine;
        uint32_t* dst32 = reinterpret_cast<uint32_t*>(lineBuffer);
        
        // Process 2 pixels at a time (4 bytes output per iteration)
        for (int pair = 0; pair < pixelPairs; pair++) {
            // First pixel
            uint8_t r1 = src[0];
            uint8_t g1 = src[1]; 
            uint8_t b1 = src[2];
            // Skip alpha (src[3])
            src += 4;
            
            // Second pixel
            uint8_t r2 = src[0];
            uint8_t g2 = src[1];
            uint8_t b2 = src[2];
            // Skip alpha (src[3])
            src += 4;
            
            // Convert both pixels to RGB565
            uint16_t pixel1 = ((b1 & 0xF8) << 8) | ((g1 & 0xFC) << 3) | (r1 >> 3);
            uint16_t pixel2 = ((b2 & 0xF8) << 8) | ((g2 & 0xFC) << 3) | (r2 >> 3);
            
            // Pack both pixels into a 32-bit value (little-endian)
            uint32_t packed = static_cast<uint32_t>(pixel1) | (static_cast<uint32_t>(pixel2) << 16);
            
            // Apply XOR pattern to all 4 bytes at once and store
            dst32[pair] = packed ^ xorPattern32;
        }
        
        // Fill remaining 128 bytes (1920-2048) efficiently
        uint32_t* fillPtr = reinterpret_cast<uint32_t*>(lineBuffer + 1920);
        const uint32_t fillPattern = xorPattern32; // XOR with 0x00000000 is just the pattern itself
        
        // Fill in 32-bit chunks (128 bytes = 32 uint32_t)
        for (int i = 0; i < 32; i++) {
            fillPtr[i] = fillPattern;
        }
    }
};