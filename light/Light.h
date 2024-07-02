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

#ifndef ANDROID_HARDWARE_LIGHT_V2_0_LIGHT_H
#define ANDROID_HARDWARE_LIGHT_V2_0_LIGHT_H

#include <android/hardware/light/2.0/ILight.h>
#include <hardware/lights.h>
#include <hidl/Status.h>
#include <android-base/logging.h>
#include <map>
#include <mutex>
#include <vector>

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::light::V2_0::Flash;
using ::android::hardware::light::V2_0::ILight;
using ::android::hardware::light::V2_0::LightState;
using ::android::hardware::light::V2_0::Status;
using ::android::hardware::light::V2_0::Type;

using LightStateHandler = void (*)(const LightState&);

struct LightBackend {
    Type type;
    LightState state;
    LightStateHandler handler;

    explicit LightBackend(Type type, LightStateHandler handler)
        : type(type), handler(handler), state{0xff000000} {}
};

class Light : public ILight {
  public:
    Return<Status> setLight(Type type, const LightState& state) override {
        std::lock_guard<std::mutex> lock(globalLock);

        for (auto& backend : backends) {
            if (backend.type == type) {
                backend.state = state;
                backend.handler(state);
                return Status::SUCCESS;
            }
        }

        LOG(ERROR) << "Failed to set light for type " << static_cast<int>(type);
        return Status::LIGHT_NOT_SUPPORTED;
    }

    Return<void> getSupportedTypes(getSupportedTypes_cb _hidl_cb) override {
        std::vector<Type> types;
        for (const auto& backend : backends) {
            types.push_back(backend.type);
        }
        _hidl_cb(types);
        return Void();
    }

  private:
    std::mutex globalLock;
    std::vector<LightBackend> backends = {
        { Type::ATTENTION, {}, handleNotification },
        { Type::NOTIFICATIONS, {}, handleNotification },
        { Type::BATTERY, {}, handleNotification },
        { Type::BACKLIGHT, {}, handleBacklight },
    };

    static void handleBacklight(const LightState& state) {
        // Implement backlight handling
        auto maxBrightnessOpt = getMaxBrightness(LCD_LED MAX_BRIGHTNESS);
        if (maxBrightnessOpt) {
            uint32_t brightness = getScaledBrightness(state, *maxBrightnessOpt);
            writeFile(LCD_LED BRIGHTNESS, brightness);
        }
    }

    static void handleNotification(const LightState& state) {
        // Implement notification handling
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
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_LIGHT_V2_0_LIGHT_H
