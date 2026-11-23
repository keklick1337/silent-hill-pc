// Keys of config.cfg that no longer do anything. A user's old file still carries them, so the loader skips them
// without a warning (an old config never produces errors) and the write-back never adds them again.
#pragma once

#include <span>
#include <string_view>

namespace sh::config {

std::span<const std::string_view> retired_keys();
bool is_retired_key(std::string_view key);
// The key of a `key = value` line of config.cfg is retired (comments, binds and lines without '=' are not).
bool is_retired_line(std::string_view line);

}  // namespace sh::config
