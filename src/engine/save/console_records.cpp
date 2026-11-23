// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/save/console_records.h"

#include "engine/log/sh_log.h"

namespace sh::save::console {

game::SaveRecord decode_save_record(std::span<const std::uint8_t> bytes) {
    game::SaveRecord r{};
    visit_save_record(console::FieldReader(bytes), 0, r);
    return r;
}

std::vector<std::uint8_t> encode_save_record(const game::SaveRecord& record) {
    std::vector<std::uint8_t> bytes(kSaveRecordBytes, 0);
    const console::FieldWriter writer(bytes);
    visit_save_record(writer, 0, record);
    if (writer.narrowed() != 0) {
        SH_LOG_WARN(SAVE,
                    "console save record: %d value(s) past the console's range (a mod's item or map) written as none",
                    static_cast<int>(writer.narrowed()));
    }
    return bytes;
}

game::Settings decode_settings(std::span<const std::uint8_t> bytes) {
    game::Settings s{};
    visit_settings(console::FieldReader(bytes), 0, s);
    return s;
}

std::vector<std::uint8_t> encode_settings(const game::Settings& settings) {
    std::vector<std::uint8_t> bytes(kSettingsBytes, 0);
    visit_settings(console::FieldWriter(bytes), 0, settings);
    return bytes;
}

}  // namespace sh::save::console
