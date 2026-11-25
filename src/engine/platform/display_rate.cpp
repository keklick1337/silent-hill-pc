// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/platform/display_rate.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "engine/log/sh_log.h"
#include "engine/platform/host_state.h"

#ifdef __ANDROID__
#include <android/native_window.h>
#include <dlfcn.h>
#include <jni.h>
#endif

namespace sh::platform {

namespace {

#ifdef __ANDROID__
// The activity's static String displayRates(String): the rates of the display's modes at its current size, joined
// by commas (android/.../SilentHillActivity.java).
std::string activity_rates() {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (env == nullptr || activity == nullptr) return {};
    std::string out;
    jclass type = env->GetObjectClass(activity);
    jmethodID method = env->GetStaticMethodID(type, "displayRates", "(Ljava/lang/String;)Ljava/lang/String;");
    if (method != nullptr) {
        jstring arg = env->NewStringUTF("");
        auto result = static_cast<jstring>(env->CallStaticObjectMethod(type, method, arg));
        if (result != nullptr) {
            if (const char* text = env->GetStringUTFChars(result, nullptr)) {
                out = text;
                env->ReleaseStringUTFChars(result, text);
            }
            env->DeleteLocalRef(result);
        }
        env->DeleteLocalRef(arg);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
    return out;
}

// ANativeWindow_setFrameRate is Android 11 (API 30); the game starts from Android 9, so it is looked up.
using SetFrameRate = int32_t (*)(ANativeWindow*, float, int8_t);

SetFrameRate set_frame_rate_fn() {
    static SetFrameRate fn = [] {
        void* lib = dlopen("libandroid.so", RTLD_NOW);
        return lib != nullptr ? reinterpret_cast<SetFrameRate>(dlsym(lib, "ANativeWindow_setFrameRate")) : nullptr;
    }();
    return fn;
}
#endif

}  // namespace

const std::vector<int>& display_rates() {
    static std::vector<int> rates;
    static bool asked = false;
#ifdef __ANDROID__
    if (!asked && g_window != nullptr) {
        asked = true;
        const std::string text = activity_rates();
        std::size_t at = 0;
        while (at < text.size()) {
            std::size_t end = text.find(',', at);
            if (end == std::string::npos) end = text.size();
            const int hz = static_cast<int>(std::lround(std::atof(text.substr(at, end - at).c_str())));
            if (hz > 0 && std::find(rates.begin(), rates.end(), hz) == rates.end()) rates.push_back(hz);
            at = end + 1;
        }
        std::sort(rates.begin(), rates.end());
        SH_LOG("[DISPLAY] refresh rates offered: %s", text.empty() ? "unknown" : text.c_str());
    }
#endif
    (void)asked;
    return rates;
}

std::vector<int> phone_frame_caps() {
    std::vector<int> caps = {30, 60};
    const std::vector<int>& rates = display_rates();
    if (std::any_of(rates.begin(), rates.end(), [](int hz) { return hz >= 119; })) caps.push_back(120);
    return caps;
}

void request_frame_rate(int fps) {
#ifdef __ANDROID__
    static void* asked_window = nullptr;
    static int asked_fps = -1;
    if (g_window == nullptr) return;
    void* window =
        SDL_GetPointerProperty(SDL_GetWindowProperties(g_window), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
    if (window == nullptr || (window == asked_window && fps == asked_fps)) return;
    asked_window = window;
    asked_fps = fps;
    const SetFrameRate fn = set_frame_rate_fn();
    if (fn == nullptr) {
        SH_LOG("[DISPLAY] the system cannot be asked for a frame rate (before Android 11)");
        return;
    }
    // A fixed source: the display runs at the rate or at a multiple of it, so every frame shows equally long.
    const int32_t r = fn(static_cast<ANativeWindow*>(window), static_cast<float>(std::max(fps, 0)),
                         ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
    SH_LOG("[DISPLAY] asked the display for %d Hz (%s)", fps, r == 0 ? "ok" : "refused");
#else
    (void)fps;
#endif
}

}  // namespace sh::platform
