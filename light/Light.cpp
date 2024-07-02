/*
 * Copyright (C) 2018 The LineageOS Project
 * Copyright (C) 2024 Hadad <hadad@linuxmail.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "LightService"

#include <fstream>
#include <log/log.h>
#include <optional>
#include <string>
#include <vector>
#include "Light.h"

namespace {

constexpr char LEDS[] = "/sys/class/leds/";
constexpr char LCD_LED[] = LEDS "lcd-backlight/";
constexpr char WHITE_LED[] = LEDS "red/";
constexpr char BREATH[] = "breath";
constexpr char BRIGHTNESS[] = "brightness";
constexpr char DELAY_OFF[] = "delay_off";
constexpr char DELAY_ON[] = "delay_on";
constexpr char MAX_BRIGHTNESS[] = "max_brightness";

bool writeFile(const std::string& path, const std::string& value) {
    std::ofstream file(path);
    if (!file.is_open()) {
        ALOGW("Failed to write %s to %s", value.c_str(), path.c_str());
        return false;
    }
    file << value;
    file.close(); // Close file after writing
    return true;
}

bool writeFile(const std::string& path, int value) {
    return writeFile(path, std::to_string(value));
}

std::optional<int> readFile(const std::string& path) {
    std::ifstream file(path);
    int value;
    if (!file.is_open()) {
        ALOGW("Failed to read from %s", path.c_str());
        return std::nullopt;
    }
    file >> value;
    file.close(); // Close file after reading
    return value;
}

std::optional<int> getMaxBrightness(const std::string& path) {
    auto value = readFile(path);
    if (value) {
        ALOGI("Got max brightness %d", *value);
    }
    return value;
}

uint32_t getBrightness(const LightState& state) {
    uint32_t alpha = (state.color >> 24) & 0xFF;
    uint32_t red = (state.color >> 16) & 0xFF;
    uint32_t green = (state.color >> 8) & 0xFF;
    uint32_t blue = state.color & 0xFF;

    if (alpha != 0xFF) {
        red = red * alpha / 0xFF;
        green = green * alpha / 0xFF;
        blue = blue * alpha / 0xFF;
    }

    return (77 * red + 150 * green + 29 * blue) >> 8;
}

uint32_t scaleBrightness(uint32_t brightness, uint32_t maxBrightness) {
    return (brightness * maxBrightness) / 255; // Direct division operation
}

uint32_t getScaledBrightness(const LightState& state, uint32_t maxBrightness) {
    return scaleBrightness(getBrightness(state), maxBrightness);
}

void handleBacklight(const LightState& state) {
    auto maxBrightnessOpt = getMaxBrightness(LCD_LED MAX_BRIGHTNESS);
    if (maxBrightnessOpt) {
        uint32_t brightness = getScaledBrightness(state, *maxBrightnessOpt);
        writeFile(LCD_LED BRIGHTNESS, brightness);
    }
}

void handleNotification(const LightState& state) {
    auto maxBrightnessOpt = getMaxBrightness(WHITE_LED MAX_BRIGHTNESS);
    if (maxBrightnessOpt) {
        uint32_t whiteBrightness = getScaledBrightness(state, *maxBrightnessOpt);
        writeFile(WHITE_LED BREATH, 0); // Disable blinking

        if (state.flashMode == Flash::TIMED) {
            writeFile(WHITE_LED DELAY_OFF, state.flashOffMs);
            writeFile(WHITE_LED DELAY_ON, state.flashOnMs);
            writeFile(WHITE_LED BREATH, 1); // Enable blinking
        } else {
            writeFile(WHITE_LED BRIGHTNESS, whiteBrightness);
        }
    }
}

bool isLit(const LightState& state) {
    return state.color & 0x00ffffff;
}

} // anonymous namespace

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

Return<Status> Light::setLight(Type type, const LightState& state) {
    LightStateHandler handler = nullptr;
    bool handled = false;

    // Lock global mutex until light state is updated
    std::lock_guard<std::mutex> lock(globalLock);

    // Update cached state value for current type
    for (auto& backend : backends) {
        if (backend.type == type) {
            backend.state = state;
            handler = backend.handler;
        }
    }

    // Return LIGHT_NOT_SUPPORTED if no handler is found
    if (!handler) {
        return Status::LIGHT_NOT_SUPPORTED;
    }

    // Light up type with highest priority matching handler
    for (auto& backend : backends) {
        if (handler == backend.handler && isLit(backend.state)) {
            handler(backend.state);
            handled = true;
            break;
        }
    }

    // Turn off hardware if no type is lit up
    if (!handled) {
        handler(state);
    }

    return Status::SUCCESS;
}

Return<void> Light::getSupportedTypes(getSupportedTypes_cb _hidl_cb) {
    std::vector<Type> types;

    // Lock global mutex until supported types are retrieved
    std::lock_guard<std::mutex> lock(globalLock);

    // Populate vector with supported types
    for (const auto& backend : backends) {
        types.push_back(backend.type);
    }

    // Call HIDL callback with supported types
    _hidl_cb(types);

    return Void();
}

} // namespace implementation
} // namespace V2_0
} // namespace light
} // namespace hardware
} // namespace android
