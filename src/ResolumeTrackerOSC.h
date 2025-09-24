#pragma once

#define TRACKING_WITH_OSC
#ifndef TRACKING_WITH_REST

#include <vector>
#include <string>
#include <map>
#include <memory>
#include <iostream>
#include <variant>
#include <sstream>
#include <functional>
#include <thread>
#include <atomic>
#include "PropertyDictionary.h"

// Include the ResolumeOSCListener header to provide the full type definition
#include "OSCListener.h"

// Helper function to debug OSC
inline void debugOSC(const std::vector<std::string>& pathParts, const std::vector<float>& floats, 
                          const std::vector<int>& integers, const std::vector<std::string>& strings) {
    std::cout << "OSC Path: ";
    for (size_t i = 0; i < pathParts.size(); ++i) {
        if (i > 0) std::cout << "/";
        std::cout << pathParts[i];
    }

    if(!floats.empty()) {
        std::cout << " | Floats: [";
        for (size_t i = 0; i < floats.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << floats[i];
        }
        std::cout << "]";
    }
    if(!integers.empty()) {
        std::cout << " | Ints: [";
        for (size_t i = 0; i < integers.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << integers[i];
        }
        std::cout << "]";
    }
    if(!strings.empty()) {
        std::cout << " | Strings: [";
        for (size_t i = 0; i < strings.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << strings[i];
        }
        std::cout << "]";
    }

    std::cout << std::endl;
}

// Helper function to split OSC address into path components
inline std::vector<std::string> splitOSCPath(const std::string& address) {
    std::vector<std::string> parts;
    if (address.empty() || address[0] != '/') return parts;
    
    size_t start = 1; // Skip initial slash
    size_t pos = start;
    
    while (pos < address.length()) {
        if (address[pos] == '/') {
            if (pos > start) {
                parts.push_back(address.substr(start, pos - start));
            }
            start = pos + 1;
        }
        pos++;
    }
    
    // Add final part if exists
    if (start < address.length()) {
        parts.push_back(address.substr(start));
    }
    
    return parts;
}

class Effect {
public:
    int id;
    std::string name;
    PropertyDictionary properties;
    
    Effect(int effectId, const std::string& effectName) 
        : id(effectId), name(effectName) {}
    
    void processOSCMessage(const std::vector<std::string>& pathParts, const std::vector<float>& floats, 
                          const std::vector<int>& integers, const std::vector<std::string>& strings) {
        if (pathParts.empty()) {
            // Root effect property
            properties.setFromOSCData("", floats, integers, strings);
        } else {
            // Build endpoint from remaining path
            std::string endpoint = "";
            for (size_t i = 0; i < pathParts.size(); ++i) {
                if (i > 0) endpoint += "/";
                endpoint += pathParts[i];
            }
            properties.setFromOSCData(endpoint, floats, integers, strings);
        }
    }
    
    void clear() {
        properties.clear();
    }
    
    // Print method for trickle-down printing
    void print(const std::string& indent) const {
        std::cout << indent << "Effect: " << name << " (ID: " << id << ")" << std::endl;
        if (!properties.properties.empty()) {
            std::cout << indent << "  Properties:" << std::endl;
            properties.print(indent + "    ");
        }
    }
};

class Clip {
public:
    int id;
    std::string name;
    PropertyDictionary properties;
    std::vector<std::shared_ptr<Effect>> effects;
    
    // Add timing tracking for transport position
    mutable std::chrono::steady_clock::time_point lastTransportUpdate;

    Clip(int clipId) : id(clipId), name("") {
        lastTransportUpdate = std::chrono::steady_clock::now();
    }

    void setName(const std::string& clipName) { name = clipName; }

    bool exists() const {
        // check if the properties is more than 3
        return properties.size() > 3;
    }

    bool playing() const {
        // Check if we have a recent transport update AND position > 0
        auto now = std::chrono::steady_clock::now();
        auto timeSinceUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTransportUpdate);

        // If no transport update in the last 50ms, consider it stopped
        return timeSinceUpdate.count() < 100 && properties.getFloat("transport/position") > 0;
        
        //return properties.getFloat("transport/position") > 0;
    }

    // Force expire the timeout by setting lastTransportUpdate to a very old time
    void forceExpire() {
        //lastTransportUpdate = std::chrono::steady_clock::now() - std::chrono::milliseconds(1000);
        // set transport position to 0
        properties.setFloat("transport/position", 0.0f);
    }

    std::shared_ptr<Effect> getOrCreateEffect(const std::string& effectName) {
        // Find existing effect
        for (auto& effect : effects) {
            if (effect->name == effectName) {
                return effect;
            }
        }
        
        // Create new effect
        int newId = effects.size() + 1;
        auto newEffect = std::make_shared<Effect>(newId, effectName);
        effects.push_back(newEffect);
        return newEffect;
    }
    
    void processOSCMessage(const std::vector<std::string>& pathParts, const std::vector<float>& floats, 
                          const std::vector<int>& integers, const std::vector<std::string>& strings) {
        if (pathParts.empty()) {
            // Direct clip property
            properties.setFromOSCData("", floats, integers, strings);
            return;
        }
        
        std::vector<std::string> parts = pathParts; // Make a copy to modify
        
        // Handle clip name specially
        if (parts.size() == 1 && parts[0] == "name" && !strings.empty()) {
            setName(strings[0]);
            return;
        }
        
        // Update timestamp for transport position messages
        if (parts.size() == 2 && parts[0] == "transport" && parts[1] == "position") {
            lastTransportUpdate = std::chrono::steady_clock::now();
        }
        
        if (parts[0] == "video" && parts.size() >= 3 && parts[1] == "effects") {
            // /video/effects/effectname/...
            std::string effectName = parts[2];
            auto effect = getOrCreateEffect(effectName);
            
            // Remove "video", "effects", and effect name from path
            std::vector<std::string> remainingPath(parts.begin() + 3, parts.end());
            effect->processOSCMessage(remainingPath, floats, integers, strings);
            return;
        }
        
        // Store as general property
        std::string endpoint = "";
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) endpoint += "/";
            endpoint += parts[i];
        }
        properties.setFromOSCData(endpoint, floats, integers, strings);
    }
    
    void clear() {
        name = "";
        properties.clear();
        effects.clear();
    }
    
    // Print method for trickle-down printing
    void print(const std::string& indent) const {
        if (!properties.empty()) {
            std::cout << indent << "Clip " << id << ": <" << name << ">" << (exists() ? " exists" : "") << std::endl;
            std::cout << indent << "  Properties:" << std::endl;
            properties.print(indent + "    ");
        }
        
        if (!effects.empty()) {
            for (const auto& effect : effects) {
                effect->print(indent + "  ");
            }
        }
    }
};

class Layer {
public:
    int id;
    PropertyDictionary properties;
    std::vector<std::shared_ptr<Effect>> effects;
    std::vector<std::shared_ptr<Clip>> clips;
    //int mostRecentPlayingClipId; // Track most recently playing clip in this layer

    Layer(int layerId) : id(layerId) {
    }

    //int getPlayingId() const {
    //    return mostRecentPlayingClipId;
    //}

    std::shared_ptr<Clip> getOrCreateClip(int clipId) {
        if (clipId < 1) return nullptr;
        // Dynamically grow the clips vector as needed
        if (clipId > static_cast<int>(clips.size())) {
            clips.resize(clipId);
            for (int i = 0; i < clipId; ++i) {
                if (!clips[i]) clips[i] = std::make_shared<Clip>(i + 1);
            }
        }
        return clips[clipId - 1];
    }

    std::shared_ptr<Clip> getClip(int clipId) {
        if (clipId < 1) return nullptr;
        if (clipId > static_cast<int>(clips.size())) return nullptr;
        return clips[clipId - 1];
    }

    std::shared_ptr<Effect> getOrCreateEffect(const std::string& effectName) {
        // Find existing effect
        for (auto& effect : effects) {
            if (effect->name == effectName) {
                return effect;
            }
        }
        
        // Create new effect
        int newId = effects.size() + 1;
        auto newEffect = std::make_shared<Effect>(newId, effectName);
        effects.push_back(newEffect);
        return newEffect;
    }
    
    void processOSCMessage(const std::vector<std::string>& pathParts, const std::vector<float>& floats, 
                          const std::vector<int>& integers, const std::vector<std::string>& strings) {
        try {
            if (pathParts.empty()) {
                // Direct layer property
                properties.setFromOSCData("", floats, integers, strings);
                return;
            }
            
            std::vector<std::string> parts = pathParts; // Make a copy to modify
            
            if (parts[0] == "clips" && parts.size() >= 2) {
                // /clips/X/...
                // check if parts[1] starts with a digit. If it doesn't its probably transitiontarget, so ignore it.
                if (parts[1].empty() || !std::isdigit(parts[1][0])) return;

                int clipId = std::stoi(parts[1]);
                auto clip = getOrCreateClip(clipId);
                if (clip) {
                    // Remove "clips" and clip number from path
                    std::vector<std::string> remainingPath(parts.begin() + 2, parts.end());
                    clip->processOSCMessage(remainingPath, floats, integers, strings);
                }

                return;
            }
            
            if (parts[0] == "video" && parts.size() >= 3 && parts[1] == "effects") {
                // /video/effects/effectname/...
                std::string effectName = parts[2];
                auto effect = getOrCreateEffect(effectName);
                
                // Remove "video", "effects", and effect name from path
                std::vector<std::string> remainingPath(parts.begin() + 3, parts.end());
                effect->processOSCMessage(remainingPath, floats, integers, strings);
                return;
            }
            
            // Store as general property
            std::string endpoint = "";
            for (size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) endpoint += "/";
                endpoint += parts[i];
            }
            properties.setFromOSCData(endpoint, floats, integers, strings);
        } catch (Exception& e) {
            std::cerr << "Error processing OSC message in Layer " << id << ": " << e.what() << std::endl;
        }
    }
    
    void clear() {
        properties.clear();
        effects.clear();
        for (auto& clip : clips) {
            clip->clear();
        }
    }
    
    // Timeout all clips except the specified clip index (1-based)
    void timeoutAllExcept(int exceptClipId) {
        //std::cout << "Timeout all clips except " << exceptClipId << " in layer " << id << std::endl;
        
        for (auto& clip : clips) {
            if (clip && clip->id != exceptClipId) {
                clip->forceExpire();
            }
        }
    }

    // Print method for trickle-down printing
    void print(const std::string& indent) const {
        std::cout << indent << "Layer: " << std::endl;

        if (!properties.properties.empty()) {
            std::cout << indent << "  Properties:" << std::endl;
            properties.print(indent + "    ");
        }
        
        if (!effects.empty()) {
            for (const auto& effect : effects) {
                effect->print(indent + "  ");
            }
        }
        
        if (!clips.empty()) {
            std::cout << indent << "  Clips:" << std::endl;
            for (const auto& clip : clips) {
                clip->print(indent + "    ");
            }
        }
    }
};

class ResolumeTracker {
private:
    std::vector<std::shared_ptr<Layer>> layers;

    // Centralized selection/connection state
    int selectedColumnId = 0;
    int selectedLayerId = 0;
    int selectedClipLayerId = 0;
    int selectedClipId = 0;
    int selectedDeckId = 0;
    int connectedColumnId = 0;

    // Track current deck to detect changes
    int currentDeckId;
    bool deckInitialized;
    
    // Track most recently selected layer and clip
    int lastSelectedLayerId;
    int lastSelectedClipLayerId;  // Which layer the selected clip is in
    int lastSelectedClipId;       // Which clip within that layer
    
    // Track timing to determine which effects bus to use
    enum class LastSelectionType {
        NONE,
        LAYER,
        CLIP
    };
    LastSelectionType lastSelectionType;

    // Add reference to OSC listener for queries
    ResolumeOSCListener* oscListener = nullptr;
    
    // Callback for selection changes
    std::function<void()> selectionChangeCallback = nullptr;
    
    // Message processing thread
    std::thread processingThread;
    std::atomic<bool> shouldStopProcessing{false};
    
    void messageProcessingLoop() {
        while (!shouldStopProcessing.load()) {
            if (oscListener) {
                auto message = oscListener->getNextMessage();
                if (message.has_value()) {
                    processOSCMessage(message->address, message->floats, message->integers, message->strings);
                } else {
                    // Increase sleep time to reduce CPU usage and potential race conditions
                    std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Changed from microseconds(10)
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

public:
    ResolumeTracker(ResolumeOSCListener* listener = nullptr) : currentDeckId(0), deckInitialized(false),
                       lastSelectedLayerId(0), lastSelectedClipLayerId(0), lastSelectedClipId(0),
                       lastSelectionType(LastSelectionType::NONE), oscListener(listener)
    {
        
        // Set up the callback if OSCListener is provided
        if (oscListener) {
            // Remove the direct callback since we're using the queue now
        }
        
        // Start the message processing thread
        shouldStopProcessing.store(false);
        processingThread = std::thread(&ResolumeTracker::messageProcessingLoop, this);
    }
    
    ~ResolumeTracker() {
        // Stop the processing thread
        shouldStopProcessing.store(true);
        if (processingThread.joinable()) {
            processingThread.join();
        }
    }
    
    void setOSCListener(ResolumeOSCListener* listener) {
        oscListener = listener;
        // No need to set callback anymore since we're using the queue
    }
    
    // Set callback for selection changes
    void setSelectionChangeCallback(std::function<void()> callback) {
        selectionChangeCallback = callback;
    }
    
    // Returns the number of layers
    int getLayerCount() const {
        return static_cast<int>(layers.size());
    }

    // Returns the maximum number of clips in any layer (number of columns)
    int getColumnCount() const {
        int maxClips = 0;
        for (const auto& layer : layers) {
            if (!layer) continue;
            int count = 0;
            for (const auto& clip : layer->clips) {
                if (clip && !clip->name.empty()) ++count;
            }
            if (count > maxClips) maxClips = count;
        }
        return maxClips;
    }

    void processOSCMessage(const std::string& address, const std::vector<float>& floats,
                           const std::vector<int>& integers, const std::vector<std::string>& strings) {
        try {
            // Only process /composition messages
            if (address.find("/composition") != 0) return;

            //if(!floats.empty()) return; // Ignore float messages for now

            // Split the address into components
            std::vector<std::string> pathParts = splitOSCPath(address);
            
            // Add validation
            if (pathParts.empty()) return;
            
            // Remove "composition" from the beginning
            if (pathParts[0] != "composition") return;
            pathParts.erase(pathParts.begin());

            if (pathParts.empty()) return;

            // --- 1. Handle deck selection and deck change ---
            if (pathParts[0] == "decks" && pathParts.size() >= 3) {
                int deckId = std::stoi(pathParts[1]);

                //debugOSC(pathParts, floats, integers, strings);

                if (pathParts[2] == "select" && integers.empty()) { // && integers[0] == 1 apparently select is sent with no payload
                    if (deckId != currentDeckId) {
                        //std::cout << "Deck changed to: " << deckId << std::endl;
                        clear();
                        currentDeckId = deckId;
                    }
                }
                return;
            }

            // Check for select/connect messages
            std::string endpoint = pathParts.back();
            bool isSelect = (endpoint == "select");
            bool isConnect = (endpoint == "connect");

            if (isSelect || (isConnect && ((!integers.empty() && integers[0] == 1) || (!floats.empty() && floats[0] == 1.0f)))) {
                //std::cout << "Select/Connect Message: " << address << std::endl;
                //debugOSC(pathParts, floats, integers, strings);

                if (pathParts[0] == "columns") {
                    int columnId = std::stoi(pathParts[1]);
                    if (isSelect) {
                        int prevSelectedColumnId = selectedColumnId;
                        selectedColumnId = columnId;
                        if (prevSelectedColumnId != selectedColumnId && selectionChangeCallback) {
                            selectionChangeCallback();
                        }
                    } else if (isConnect) {
                        connectedColumnId = columnId;
                    }
                    return;
                }

                if (pathParts[0] == "layers") {
                    int layerId = std::stoi(pathParts[1]);
                    if (pathParts.size() == 3 && pathParts[2] == "select") {
                        //debugOSC(pathParts, floats, integers, strings);
                        // /layers/X/select
                        int prevSelectedLayerId = selectedLayerId;
                        selectedLayerId = layerId;
                        if (prevSelectedLayerId != selectedLayerId && selectionChangeCallback) {
                            selectionChangeCallback();
                        }
                        return;
                    } else if (pathParts.size() >= 4 && pathParts[2] == "clips") {
                        // check if pathParts[3] starts with a digit. If it doesn't its probably transitiontarget, so ignore it.
                        if (pathParts[3].empty() || !std::isdigit(pathParts[3][0])) return;

                        // /layers/X/clips/Y/select or /connect
                        int clipId = std::stoi(pathParts[3]);
                        if (isSelect) {
                            int prevSelectedClipLayerId = selectedClipLayerId;
                            int prevSelectedClipId = selectedClipId;
                            selectedClipLayerId = layerId;
                            selectedClipId = clipId;
                            if ((prevSelectedClipLayerId != selectedClipLayerId || prevSelectedClipId != selectedClipId) && selectionChangeCallback) {
                                selectionChangeCallback();
                            }
                        } else if (isConnect) {
                            // TODO maybe disconnectall?
                        }
                        return;
                    } else {
                        // Unhandled /layers/X/...
                        debugOSC(pathParts, floats, integers, strings);
                    }
                }
            }

            // Skip certain endpoints
            if (endpoint == "selected" || endpoint == "connected") {
                return;
            }

            // --- 3. Trickledown: pass to appropriate layer/clip/effect ---
            if (pathParts[0] == "layers" && pathParts.size() >= 2) {
                int layerId = std::stoi(pathParts[1]);
                auto layer = getOrCreateLayer(layerId); // Use getOrCreateLayer instead of getLayer
                if (layer) {
                    // Remove "layers" and layer number from path
                    std::vector<std::string> remainingPath(pathParts.begin() + 2, pathParts.end());
                    layer->processOSCMessage(remainingPath, floats, integers, strings);
                }
                return;
            }

            // Ignore other top-level paths
            if (pathParts[0] == "columns" || pathParts[0] == "decks" || 
                pathParts[0] == "selectedlayer" || pathParts[0] == "selectedclip" || 
                pathParts[0] == "selectedcolumn") {
                return;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error processing OSC message '" << address << "': " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown error processing OSC message: " << address << std::endl;
        }
    }
    
    std::shared_ptr<Layer> getOrCreateLayer(int layerId) {
        if (layerId < 1) return nullptr;
        
        // Add safety check for maximum reasonable layer count
        if (layerId > 100) { // Reasonable upper limit
            std::cerr << "Warning: Layer ID " << layerId << " exceeds reasonable limit" << std::endl;
            return nullptr;
        }
        
        // Dynamically grow the layers vector as needed
        if (layerId > static_cast<int>(layers.size())) {
            try {
                layers.resize(layerId);
                for (int i = 0; i < layerId; ++i) {
                    if (!layers[i]) layers[i] = std::make_shared<Layer>(i + 1);
                }
            } catch (const std::exception& e) {
                std::cerr << "Error resizing layers vector: " << e.what() << std::endl;
                return nullptr;
            }
        }
        return layers[layerId - 1];
    }
    
    std::shared_ptr<Layer> getLayer(int layerId) {
        if (layerId >= 1 && layerId <= static_cast<int>(layers.size())) {
            return layers[layerId - 1];
        }
        return nullptr;
    }
    
    //std::shared_ptr<const Layer> getLayer(int layerId) const {
    //    if (layerId >= 1 && layerId <= static_cast<int>(layers.size())) {
    //        return layers[layerId - 1];
    //    }
    //    return nullptr;
    //}

    std::shared_ptr<const Layer> getSelectedLayer() const {
        if (selectedLayerId >= 1 && selectedLayerId <= static_cast<int>(layers.size())) {
            return layers[selectedLayerId - 1];
        }
        return nullptr;
    }

    int getCurrentDeck() const {
        return currentDeckId;
    }
    
    // Convenience getters for commonly used values
    //bool isTempoControllerPlaying() const { 
    //    return deckProperties.getInt("tempocontroller/play", 0) == 1;
    //}
    int getSelectedLayerId() const { return selectedLayerId; }
    int getSelectedColumn() const { return selectedColumnId; }
    int getConnectedColumn() const { return connectedColumnId; }
    int getCurrentDeckId() const { return currentDeckId; }
    bool isDeckInitialized() const { return deckInitialized; }
    
    
    std::pair<int, int> getSelectedClip() const {
        return std::make_pair(selectedClipLayerId, selectedClipId);
    }
    
    // Get the most recently selected effects bus
    std::vector<std::shared_ptr<Effect>>* getSelectedEffectsBus() {
        if (lastSelectionType == LastSelectionType::CLIP && selectedClipLayerId > 0 && selectedClipId > 0) {
            auto layer = getLayer(selectedClipLayerId);
            if (layer) {
                auto clip = layer->getClip(selectedClipId);
                if (clip) {
                    return &clip->effects;
                }
            }
        } else if (lastSelectionType == LastSelectionType::LAYER && selectedLayerId > 0) {
            auto layer = getLayer(selectedLayerId);
            if (layer) {
                return &layer->effects;
            }
        }
        
        // Fallback: if we have a selected clip, return its effects
        if (selectedClipLayerId > 0 && selectedClipId > 0) {
            auto layer = getLayer(selectedClipLayerId);
            if (layer) {
                auto clip = layer->getClip(selectedClipId);
                if (clip) {
                    return &clip->effects;
                }
            }
        }
        
        // Fallback: if we have a selected layer, return its effects
        if (selectedLayerId > 0) {
            auto layer = getLayer(selectedLayerId);
            if (layer) {
                return &layer->effects;
            }
        }
        
        return nullptr;
    }
    
    //PropertyDictionary& getDeckProperties() { return deckProperties; }
    //const PropertyDictionary& getDeckProperties() const { return deckProperties; }
    
    // Method to manually set/change deck (useful for testing)
    void setCurrentDeck(int deckId) {
        if (deckInitialized && deckId != currentDeckId) {
            std::cout << "Manually changing deck from " << currentDeckId << " to " << deckId << " - clearing all data" << std::endl;
            clear();
        }
        currentDeckId = deckId;
        deckInitialized = true;
    }
    
    void clear() {
        selectedColumnId = 0;
        connectedColumnId = 0;
        selectedLayerId = 0;
        selectedClipLayerId = 0;
        selectedClipId = 0;
        lastSelectionType = LastSelectionType::NONE;
        //deckProperties.clear();

        //clear the queue of messages
        if (oscListener) {
            oscListener->clearMessageQueue();
        }

        std::cout << "Queue cleared" << std::endl;
        
        layers.clear();
        
        //for (auto& layer : layers) {
        //    layer->clear();
        //}
        // Removed: prevLayerCount and prevColumnCount reset
    }
    
    // Additional convenience methods for PushUI integration
    bool doesClipExist(int column, int layer) {
        // Fallback to old method if no OSC listener available
        auto layerObj = getLayer(layer);
        if (!layerObj) return false;
        auto clipObj = layerObj->getClip(column);
        return clipObj && clipObj->exists();
    }
    
    bool isColumnConnected(int column) {
        return getConnectedColumn() == column;
    }

    bool isClipPlaying(int column, int layer) {
        auto layerObj = getLayer(layer);
        if (!layerObj) return false;
        //return layerObj->getPlayingId() == column;
        
        auto clipObj = layerObj->getClip(column);
        if(clipObj) { // need to use this because getClip is not guaranteed to return a valid clip
            return clipObj->playing();
        }
        return false;
    }
    
    bool doesLayerExist(int layer) {
        auto layerObj = getLayer(layer);
        if (!layerObj) return false;

        // Only check existing clips, do not create new ones
        for (const auto& clip : layerObj->clips) {
            if (clip && !clip->name.empty()) {
                return true;
            }
        }
        return false;
    }

    // Print method for trickle-down printing
    void print(const std::string& indent = "") const {
        std::cout << indent << "ResolumeTracker:" << std::endl;
        std::cout << indent << "  Current Deck: " << currentDeckId << " (Initialized: " << (deckInitialized ? "Yes" : "No") << ")" << std::endl;
        std::cout << indent << "  Selected Column: " << selectedColumnId << ", Connected Column: " << connectedColumnId << std::endl;
        std::cout << indent << "  Selected Layer: " << selectedLayerId << ", Selected Clip: " << selectedClipId << " (Layer " << selectedClipLayerId << ")" << std::endl;
        
        //if (!deckProperties.properties.empty()) {
        //    std::cout << indent << "  Deck Properties:" << std::endl;
        //    deckProperties.print(indent + "    ");
        //}
        
        if (!layers.empty()) {
            std::cout << indent << "  Layers:" << std::endl;
            for (const auto& layer : layers) {
                layer->print(indent + "    ");
            }
        }
    }
};

#endif // TRACKING_WITH_REST