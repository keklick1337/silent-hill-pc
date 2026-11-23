#include "engine/mods/semver.h"

#include <charconv>
#include <cstddef>
#include <optional>
#include <system_error>

namespace sh::mods {

namespace {

constexpr std::string_view kBlanks = " \t\r\n";

std::string_view trim(std::string_view text) {
    const std::size_t first = text.find_first_not_of(kBlanks);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(kBlanks) - first + 1);
}

std::optional<int> parse_part(std::string_view text) {
    if (text.empty() || text.size() > 9) {
        return std::nullopt;
    }
    int value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || ptr != text.data() + text.size() || value < 0) {
        return std::nullopt;
    }
    return value;
}

bool is_wildcard(std::string_view part) {
    return part == "x" || part == "X" || part == "*";
}

// A version as written in a range: how many leading parts were given (wildcards end the count).
struct Partial {
    Version version;
    int parts = 0;
};

Result<Partial> parse_partial(std::string_view text) {
    Partial out;
    std::string_view rest = text;
    if (const std::size_t plus = rest.find('+'); plus != std::string_view::npos) {
        rest = rest.substr(0, plus);
    }
    if (const std::size_t dash = rest.find('-'); dash != std::string_view::npos) {
        out.version.prerelease = std::string(rest.substr(dash + 1));
        rest = rest.substr(0, dash);
        if (out.version.prerelease.empty()) {
            return Result<Partial>::fail("empty pre-release in '" + std::string(text) + "'");
        }
    }
    int* fields[3] = {&out.version.major, &out.version.minor, &out.version.patch};
    bool wild = false;
    int index = 0;
    while (true) {
        const std::size_t dot = rest.find('.');
        const std::string_view part = rest.substr(0, dot);
        if (index >= 3) {
            return Result<Partial>::fail("too many parts in '" + std::string(text) + "'");
        }
        if (is_wildcard(part)) {
            wild = true;
        } else if (wild) {
            return Result<Partial>::fail("a number after a wildcard in '" + std::string(text) + "'");
        } else {
            const std::optional<int> value = parse_part(part);
            if (!value) {
                return Result<Partial>::fail("'" + std::string(text) + "' is not a version (MAJOR.MINOR.PATCH)");
            }
            *fields[index] = *value;
            out.parts = index + 1;
        }
        ++index;
        if (dot == std::string_view::npos) {
            break;
        }
        rest = rest.substr(dot + 1);
    }
    if (!out.version.prerelease.empty() && out.parts < 3) {
        return Result<Partial>::fail("a pre-release needs all three parts in '" + std::string(text) + "'");
    }
    return Result<Partial>::ok(std::move(out));
}

// The smallest version above every version that starts with the given parts ("1.2" -> 1.3.0, "1" -> 2.0.0).
Version next_after(const Partial& p) {
    Version v;
    if (p.parts <= 1) {
        v.major = p.version.major + 1;
    } else {
        v.major = p.version.major;
        v.minor = p.version.minor + 1;
    }
    return v;
}

Version release_of(Version v) {
    v.prerelease.clear();
    return v;
}

}  // namespace

std::strong_ordering operator<=>(const Version& a, const Version& b) {
    if (auto c = a.major <=> b.major; c != 0) {
        return c;
    }
    if (auto c = a.minor <=> b.minor; c != 0) {
        return c;
    }
    if (auto c = a.patch <=> b.patch; c != 0) {
        return c;
    }
    if (a.prerelease == b.prerelease) {
        return std::strong_ordering::equal;
    }
    if (a.prerelease.empty()) {
        return std::strong_ordering::greater;
    }
    if (b.prerelease.empty()) {
        return std::strong_ordering::less;
    }
    // Plain text order is enough for the "alpha" < "beta" < "rc" labels mods use.
    return a.prerelease < b.prerelease ? std::strong_ordering::less : std::strong_ordering::greater;
}

Result<Version> Version::parse(std::string_view text) {
    const std::string_view t = trim(text);
    if (t.empty()) {
        return Result<Version>::fail("empty version");
    }
    Result<Partial> p = parse_partial(t.starts_with('v') ? t.substr(1) : t);
    if (!p) {
        return Result<Version>::fail(p.error());
    }
    if (p.value().parts == 0) {
        return Result<Version>::fail("'" + std::string(t) + "' is not a version (MAJOR.MINOR.PATCH)");
    }
    return Result<Version>::ok(p.value().version);
}

std::string Version::to_string() const {
    std::string s = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    if (!prerelease.empty()) {
        s += "-" + prerelease;
    }
    return s;
}

VersionRange VersionRange::any() {
    return VersionRange();
}

Result<VersionRange> VersionRange::parse(std::string_view text) {
    VersionRange range;
    const std::string_view all = trim(text);
    if (all.empty() || all == "*" || is_wildcard(all)) {
        return Result<VersionRange>::ok(range);
    }
    range.text_ = std::string(all);

    std::string_view rest = all;
    while (true) {
        const std::size_t bar = rest.find("||");
        const std::string_view alternative = trim(rest.substr(0, bar));
        if (alternative.empty()) {
            return Result<VersionRange>::fail("empty alternative in range '" + range.text_ + "'");
        }
        Conjunction conj;
        std::string_view words = alternative;
        while (!(words = trim(words)).empty()) {
            const std::size_t end = words.find_first_of(kBlanks);
            std::string_view word = words.substr(0, end);
            words = end == std::string_view::npos ? std::string_view() : words.substr(end);

            std::string_view op_text;
            for (std::string_view candidate : {">=", "<=", ">", "<", "=", "^", "~"}) {
                if (word.starts_with(candidate)) {
                    op_text = candidate;
                    break;
                }
            }
            word.remove_prefix(op_text.size());
            if (word.empty()) {
                // ">= 1.2": the operator and its version written apart.
                words = trim(words);
                const std::size_t next = words.find_first_of(kBlanks);
                word = words.substr(0, next);
                words = next == std::string_view::npos ? std::string_view() : words.substr(next);
            }
            if (word.starts_with('v')) {
                word.remove_prefix(1);
            }
            Result<Partial> parsed = parse_partial(word);
            if (!parsed) {
                return Result<VersionRange>::fail("range '" + range.text_ + "': " + parsed.error());
            }
            const Partial& p = parsed.value();
            if (p.parts == 0) {
                if (op_text.empty() || op_text == "=" || op_text == ">=" || op_text == "<=") {
                    continue;  // "*", ">=*": no limit
                }
                return Result<VersionRange>::fail("range '" + range.text_ + "': nothing to compare with");
            }
            const Version low = p.version;
            if (op_text == "^") {
                Version high;
                if (low.major > 0 || p.parts == 1) {
                    high.major = low.major + 1;
                } else if (low.minor > 0 || p.parts == 2) {
                    high.minor = low.minor + 1;
                } else {
                    high.patch = low.patch + 1;
                }
                conj.push_back({Op::Ge, low});
                conj.push_back({Op::Lt, high});
            } else if (op_text == "~") {
                conj.push_back({Op::Ge, low});
                conj.push_back({Op::Lt, next_after(Partial{low, p.parts < 2 ? 1 : 2})});
            } else if (op_text.empty() || op_text == "=") {
                if (p.parts == 3) {
                    conj.push_back({Op::Eq, low});
                } else {
                    conj.push_back({Op::Ge, low});
                    conj.push_back({Op::Lt, next_after(p)});
                }
            } else if (op_text == ">=") {
                conj.push_back({Op::Ge, low});
            } else if (op_text == "<") {
                conj.push_back({Op::Lt, low});
            } else if (op_text == ">") {
                conj.push_back(p.parts == 3 ? Comparator{Op::Gt, low} : Comparator{Op::Ge, next_after(p)});
            } else {  // "<="
                conj.push_back(p.parts == 3 ? Comparator{Op::Le, low} : Comparator{Op::Lt, next_after(p)});
            }
        }
        if (conj.empty()) {
            // One alternative allows everything, so the whole range does.
            return Result<VersionRange>::ok(VersionRange());
        }
        range.alternatives_.push_back(std::move(conj));
        if (bar == std::string_view::npos) {
            break;
        }
        rest = rest.substr(bar + 2);
    }
    return Result<VersionRange>::ok(std::move(range));
}

bool VersionRange::matches(const Version& version) const {
    if (alternatives_.empty()) {
        return true;
    }
    for (const Conjunction& conj : alternatives_) {
        bool all = true;
        for (const Comparator& c : conj) {
            // A bound written without a pre-release (">=1.0") is meant for releases: 1.0.0-beta is below it, but a
            // pre-release of a later version (2.0.0-rc under "<2.0.0") would otherwise slip in under "<".
            const Version& v = (c.op == Op::Lt && c.version.prerelease.empty() && !version.prerelease.empty() &&
                                release_of(version) == c.version)
                                   ? c.version
                                   : version;
            switch (c.op) {
            case Op::Eq: all = v == c.version; break;
            case Op::Lt: all = v < c.version; break;
            case Op::Le: all = v <= c.version; break;
            case Op::Gt: all = v > c.version; break;
            case Op::Ge: all = v >= c.version; break;
            }
            if (!all) {
                break;
            }
        }
        if (all) {
            return true;
        }
    }
    return false;
}

}  // namespace sh::mods
