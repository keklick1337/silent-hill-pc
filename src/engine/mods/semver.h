// Versions and version ranges of mods: a small subset of semver that covers
// what a mod manifest needs. No dependencies.
#pragma once

#include "formats/common/status.h"

#include <compare>
#include <string>
#include <string_view>
#include <vector>

namespace sh::mods {

// MAJOR[.MINOR[.PATCH]][-PRERELEASE][+BUILD]. Missing parts are 0 ("1" == "1.0.0"); build metadata is ignored.
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string prerelease;  // "beta.2"; a version with one sorts before the same version without

    static Result<Version> parse(std::string_view text);
    std::string to_string() const;

    friend bool operator==(const Version& a, const Version& b) {
        return a.major == b.major && a.minor == b.minor && a.patch == b.patch && a.prerelease == b.prerelease;
    }
    friend std::strong_ordering operator<=>(const Version& a, const Version& b);
};

// A set of versions. Syntax: "*" or "" (any); "1.2.3" or "=1.2.3" (exactly); ">1.2", ">=1.2", "<2", "<=2.1";
// "^1.2" (>=1.2.0 <2.0.0; ^0.3 is >=0.3.0 <0.4.0); "~1.2" (>=1.2.0 <1.3.0); "1.x" / "1.2.x" / "1" / "1.2"
// (any version with those leading parts). Comparators separated by spaces must all hold; alternatives are
// separated by "||".
class VersionRange {
public:
    static Result<VersionRange> parse(std::string_view text);
    static VersionRange any();

    bool matches(const Version& version) const;
    bool is_any() const { return alternatives_.empty(); }
    const std::string& text() const { return text_; }  // as written ("*" for any)

private:
    enum class Op { Eq, Lt, Le, Gt, Ge };
    struct Comparator {
        Op op = Op::Eq;
        Version version;
    };
    using Conjunction = std::vector<Comparator>;

    std::vector<Conjunction> alternatives_;  // empty: any version
    std::string text_ = "*";
};

}  // namespace sh::mods
