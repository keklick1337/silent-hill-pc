// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Shared libraries loaded while the program runs (the optional FFmpeg set of the movie player), and the
// lookup of a symbol the executable itself exports. The header names no OS type, so any code may include it.

#pragma once

namespace sh::loader {

// A library the process has loaded. A plain handle: copies name the same library, and nothing unloads it but
// close() (the movie player keeps its libraries for the life of the process).
class SharedLibrary {
public:
    SharedLibrary() = default;

    // Loads by path or by bare file name (then the OS search order applies). Empty when it cannot be loaded.
    static SharedLibrary open(const char* path);

    explicit operator bool() const { return handle_ != nullptr; }
    // The address of an exported symbol; null when the library is empty or does not export it.
    void* symbol(const char* name) const;
    // Unloads the library and empties the handle; nothing happens on an empty one.
    void close();

private:
    void* handle_ = nullptr;
};

}  // namespace sh::loader
