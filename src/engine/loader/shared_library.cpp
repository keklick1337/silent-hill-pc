// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Only OS headers are included here: <windows.h> defines names that clash with the engine's own.

#include "engine/loader/shared_library.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace sh::loader {

namespace {

void* address_in(HMODULE module, const char* name) {
    return module != nullptr ? reinterpret_cast<void*>(::GetProcAddress(module, name)) : nullptr;
}

}  // namespace

SharedLibrary SharedLibrary::open(const char* path) {
    SharedLibrary library;
    library.handle_ = ::LoadLibraryA(path);
    return library;
}

void* SharedLibrary::symbol(const char* name) const { return address_in(static_cast<HMODULE>(handle_), name); }

void SharedLibrary::close() {
    if (handle_ != nullptr) ::FreeLibrary(static_cast<HMODULE>(handle_));
    handle_ = nullptr;
}

}  // namespace sh::loader

#else

#include <dlfcn.h>

namespace sh::loader {

SharedLibrary SharedLibrary::open(const char* path) {
    SharedLibrary library;
    library.handle_ = ::dlopen(path, RTLD_NOW);
    return library;
}

void* SharedLibrary::symbol(const char* name) const {
    return handle_ != nullptr ? ::dlsym(handle_, name) : nullptr;
}

void SharedLibrary::close() {
    if (handle_ != nullptr) ::dlclose(handle_);
    handle_ = nullptr;
}

}  // namespace sh::loader

#endif
