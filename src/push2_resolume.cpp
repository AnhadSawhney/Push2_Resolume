// main.cpp

// OSC pack (adjust include paths to your install)
#include <iostream>
#include <thread>
#include <atomic>
#include <memory>
#include <string>

// Push 2 USB (adjust include paths to your install)
#include "OSCSender.h"
#include "PushUI.h"
#include "PushUSB.h"
#include "PushDisplay.h"
#include "PushLights.h"
//#include "ResolumeTrackerREST.h"
#include "ResolumeTrackerOSC.h"
#include "OSCListener.h"

// ------------------------
// main()
// ------------------------
int main(int argc, char* argv[]) {
    // Defaults
    int incomingOscPort = 7000;
    std::string resolumeIp = "127.0.0.1";
    int resolumeOscPort = 6669;

    // Simple command line parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--in-port" || arg == "-i") && i + 1 < argc) {
            incomingOscPort = std::stoi(argv[++i]);
        } else if ((arg == "--out-port" || arg == "-o") && i + 1 < argc) {
            resolumeOscPort = std::stoi(argv[++i]);
        } else if ((arg == "--ip" || arg == "-a") && i + 1 < argc) {
            resolumeIp = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [--in-port <port>] [--out-port <port>] [--ip <address>]" << std::endl;
            std::cout << "  --in-port,  -i   Incoming OSC port to listen on (default: 7000)" << std::endl;
            std::cout << "  --out-port, -o   Outgoing OSC port to Resolume (default: 6669)" << std::endl;
            std::cout << "  --ip,       -a   Resolume IP address (default: 127.0.0.1)" << std::endl;
            std::cout << "  --help,     -h   Show this help message" << std::endl;
            return 0;
        }
    }

    // Check for livetree mode
    bool liveTreeMode = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--livetree") {
            liveTreeMode = true;
        }
    }
    //liveTreeMode = true;

    try {
        // 1. Create OSC sender first (shared resource)
        auto oscSender = std::make_shared<OSCSender>(resolumeIp, resolumeOscPort);
        
        // 2. Create OSC listener with the sender
        ResolumeOSCListener listener(oscSender.get());
        
        // 3. Create Resolume tracker with the listener
        ResolumeTracker resolumeTracker(&listener);

        // 4. Initialize Push 2 connection
        PushUSB pushUSB;
        if (!pushUSB.initialize()) {
            std::cerr << "Failed to initialize Push 2 MIDI" << std::endl;
            return 1;
        }
        
        bool pushConnected = pushUSB.connect();
        if (pushConnected) {
            std::cout << "Push 2 connected successfully!" << std::endl;
        } else {
            std::cout << "Push 2 not connected - continuing without Push 2" << std::endl;
        }

        // 5. Create PushUI (only if Push is connected)
        std::unique_ptr<PushUI> pushUI;
        if (pushConnected) {
            pushUI = std::make_unique<PushUI>(pushUSB, resolumeTracker, oscSender);

            // Set up MIDI callback to handle Push 2 input
            pushUSB.setMidiCallback([&pushUI](const PushMidiMessage& msg) {
                if (pushUI) {
                    pushUI->onMidiMessage(msg);
                }
            });

            if (!pushUI->initialize()) {
                std::cerr << "Failed to initialize Push UI" << std::endl;
                pushUI.reset();
                pushConnected = false;
            } else {
                std::cout << "PushUI created successfully - PushDisplay and PushLights will be created in their respective threads" << std::endl;
            }
        }

        // 6. Create UDP socket for receiving OSC messages
        UdpListeningReceiveSocket socket(IpEndpointName(IpEndpointName::ANY_ADDRESS, incomingOscPort), &listener);

        std::cout << "Push2-Resolume Controller starting..." << std::endl;
        std::cout << "Listening for OSC messages on port " << incomingOscPort << std::endl;
        std::cout << "Sending OSC messages to " << resolumeIp << ":" << resolumeOscPort << std::endl;
        std::cout << "Press 'q' + Enter to quit, 'help' for commands" << std::endl;
        
        // Start listening in a separate thread
        std::atomic<bool> shouldStop(false);
        std::thread oscThread([&socket, &shouldStop]() {
            try {
                socket.RunUntilSigInt();
            } catch (...) {
                shouldStop.store(true);
            }
        });
        
        // PushLights thread (15fps)
        std::thread lightsThread([&pushUSB, &pushUI, &shouldStop]() {
            if (!pushUI) return; // Early exit if no PushUI
            
            // Create PushLights in this thread
            std::unique_ptr<PushLights> pushLights = std::make_unique<PushLights>(pushUSB, pushUI.get());
            std::cout << "PushLights created in lights thread" << std::endl;
            
            constexpr int targetFps = 15;
            constexpr int frameTimeMs = 1000 / targetFps;
            auto lastFpsCheck = std::chrono::steady_clock::now();
            int frameCount = 0;
            
            while (!shouldStop.load()) {
                auto frameStart = std::chrono::steady_clock::now();
                
                pushLights->updateLights();
                
                frameCount++;
                
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - frameStart
                ).count();
                
                // Calculate actual FPS every second
                auto timeSinceLastCheck = std::chrono::duration_cast<std::chrono::milliseconds>(
                    frameStart - lastFpsCheck
                ).count();
                
                if (timeSinceLastCheck >= 1000) {
                    float actualFps = frameCount * 1000.0f / timeSinceLastCheck;
                    if (actualFps < targetFps - 1) { // More than 1 FPS below target
                        std::cout << "PushLights running slow: " << actualFps << " fps (target: " << targetFps << ")" << std::endl;
                    }
                    frameCount = 0;
                    lastFpsCheck = frameStart;
                }
                
                int sleepMs = frameTimeMs - static_cast<int>(elapsed);
                if (sleepMs > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
                }
            }
            
            // Clean up lights when thread exits
            pushLights->clearAllPads();
            pushLights->clearAllButtons();
            std::cout << "PushLights thread exiting" << std::endl;
        });
        
        // PushDisplay thread (60fps)
        std::thread displayThread([&pushUSB, &pushUI, &shouldStop]() {
            if (!pushUI) return; // Early exit if no PushUI
            
            // Create PushDisplay in this thread
            std::unique_ptr<PushDisplay> pushDisplay = std::make_unique<PushDisplay>(pushUSB, pushUI.get());
            std::cout << "PushDisplay created in display thread" << std::endl;
            
            constexpr int targetFps = 60;
            constexpr int frameTimeMs = 1000 / targetFps;
            auto lastFpsCheck = std::chrono::steady_clock::now();
            int frameCount = 0;
            
            while (!shouldStop.load()) {
                auto frameStart = std::chrono::steady_clock::now();
                
                pushDisplay->update();
                pushDisplay->sendToDevice();
                
                frameCount++;
                
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - frameStart
                ).count();
                
                // Calculate actual FPS every second
                auto timeSinceLastCheck = std::chrono::duration_cast<std::chrono::milliseconds>(
                    frameStart - lastFpsCheck
                ).count();
                
                if (timeSinceLastCheck >= 1000) {
                    float actualFps = frameCount * 1000.0f / timeSinceLastCheck;
                    if (actualFps < targetFps - 5) { // More than 5 FPS below target
                        std::cout << "PushDisplay running slow: " << actualFps << " fps (target: " << targetFps << ")" << std::endl;
                    }
                    frameCount = 0;
                    lastFpsCheck = frameStart;
                }
                
                int sleepMs = frameTimeMs - static_cast<int>(elapsed);
                if (sleepMs > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
                }
            }
            
            std::cout << "PushDisplay thread exiting" << std::endl;
        });
        
        // LibUSB event handling thread for async transfers
        std::thread libusbThread([&shouldStop]() {
            std::cout << "LibUSB event thread started" << std::endl;
            
            while (!shouldStop.load()) {
                // Handle libusb events with a timeout
                struct timeval timeout = {0, 100000}; // 100ms timeout
                int result = libusb_handle_events_timeout(nullptr, &timeout);
                if (result < 0 && result != LIBUSB_ERROR_INTERRUPTED) {
                    std::cerr << "LibUSB event handling error: " << libusb_error_name(result) << std::endl;
                }
            }
            
            std::cout << "LibUSB event thread exiting" << std::endl;
        });
        
        // If in livetree mode, run the live tree display loop and exit
        if (liveTreeMode) {
            while (true) {
                // Clear screen (Windows)
                std::system("cls");
                resolumeTracker.print();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            return 0;
        }

        // Wait for user input to quit
        std::string input;
        while (std::getline(std::cin, input)) {
            if (input == "q" || input == "Q") {
                break;
            } else if (input == "clear") {
                resolumeTracker.clear();
                std::cout << "Cleared all state" << std::endl;
            } /* else if (input == "status") {
                std::cout << "Tempo controller playing: " << (resolumeTracker.isTempoControllerPlaying() ? "Yes" : "No") << std::endl;
                std::cout << "Selected layer: " << resolumeTracker.getSelectedLayerId() << std::endl;
                auto selectedClip = resolumeTracker.getSelectedClip();
                std::cout << "Selected clip: layer " << selectedClip.first << ", clip " << selectedClip.second << std::endl;
                std::cout << "Selected column: " << resolumeTracker.getSelectedColumnId() << std::endl;
                std::cout << "Push 2 connected: " << (pushConnected && push.isDeviceConnected() ? "Yes" : "No") << std::endl;
            } */else if (input == "tree" || input == "print") {
                resolumeTracker.print();
            } else if (input=="refresh") {
                std::cout << "Forcing Push UI refresh" << std::endl;
                //pushUI->forceRefresh();
                // Refresh will be handled automatically by the separate threads
            } else if (input == "help") {
                std::cout << "\nAvailable commands:" << std::endl;
                std::cout << "  q/Q      - Quit the program" << std::endl;
                std::cout << "  clear    - Clear all tracked state" << std::endl;
                std::cout << "  status   - Show basic status information" << std::endl;
                std::cout << "  tree     - Print complete state tree" << std::endl;
                std::cout << "  print    - Same as tree" << std::endl;
                if (pushConnected && pushUI) {
                    std::cout << "  test     - Run Push 2 lighting test" << std::endl;
                }
                std::cout << "  help     - Show this help message" << std::endl;
                std::cout << std::endl;
            } else if (input == "clipsgrid") {
                // loop through the first 8 layers and 8 columns and print x if a clip exists else _
                for (int layer = 1; layer <= 8; ++layer) {
                    for (int col = 1; col <= 8; ++col) {
                        if (resolumeTracker.doesClipExist(col, layer)) {
                            if (resolumeTracker.isClipPlaying(col, layer)) {
                                std::cout << "O "; // O for playing clip
                            } else {
                                std::cout << "X "; // X for existing clip
                            }
                        } else {
                            std::cout << "_ ";
                        }
                    }
                    std::cout << "  (Layer " << layer << ")" << std::endl;
                }
            } else if (input == "livetree") {
                // Launch a new window running this program in livetree mode
                std::string cmd = "start \"LiveTree\" \"" + std::string(argv[0]) + "\" --livetree";
                std::system(cmd.c_str());
            }
        }
        
        shouldStop.store(true);
        
        // No need to clean up lights here - they're cleaned up in their respective threads
        
        if (oscThread.joinable()) {
            oscThread.join();
        }
        if (lightsThread.joinable()) {
            lightsThread.join();
        }
        if (displayThread.joinable()) {
            displayThread.join();
        }
        if (libusbThread.joinable()) {
            libusbThread.join();
        }
        
        std::cout << "Push2-Resolume Controller stopped." << std::endl;
        
    } catch (Exception& e) {
        std::cerr << "OSC Error: " << e.what() << std::endl;
        return 1;
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}