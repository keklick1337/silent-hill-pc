#include "engine/app/first_run/picked_document.h"

#ifdef __ANDROID__
#include <SDL3/SDL_system.h>
#include <jni.h>

#include <cstdio>
#endif

namespace sh::first_run {

#ifdef __ANDROID__
namespace {

// A static method of the activity's class: String name(String). Null when it fails (said on stderr).
std::string call_activity(const char* name, const std::string& argument) {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (env == nullptr || activity == nullptr) {
        return {};
    }
    std::string out;
    jclass type = env->GetObjectClass(activity);
    jmethodID method = env->GetStaticMethodID(type, name, "(Ljava/lang/String;)Ljava/lang/String;");
    if (method != nullptr) {
        jstring arg = env->NewStringUTF(argument.c_str());
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
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        std::fprintf(stderr, "first run: %s failed\n", name);
    }
    env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
    return out;
}

constexpr const char* kContentScheme = "content://";

}  // namespace

std::string picked_document(const std::string& picked) {
    if (picked.rfind(kContentScheme, 0) != 0) {
        return picked;
    }
    const std::string path = call_activity("openDocument", picked);
    return path.empty() ? picked : path;
}

bool is_picked_document(const std::string& path) {
    return path.rfind(kContentScheme, 0) == 0 || call_activity("isPickedDocument", path) == "1";
}

void forget_picked_documents() { call_activity("forgetDocuments", std::string()); }

std::string activity_call(const char* name, const std::string& argument) { return call_activity(name, argument); }

#else

std::string picked_document(const std::string& picked) { return picked; }

bool is_picked_document(const std::string&) { return false; }

void forget_picked_documents() {}

std::string activity_call(const char*, const std::string&) { return {}; }

#endif

}  // namespace sh::first_run
