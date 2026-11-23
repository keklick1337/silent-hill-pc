// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The bone model stream and the bone link order (engine/world/bone_order.h): recorded streams, a long seeded run
// against its recorded digest, and the rules in words.

#include "framework/golden.h"
#include "framework/sh_test.h"

#include "engine/world/bone_order.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

using namespace sh::world;

namespace {

constexpr const char* kTable = "world/bone_stream.txt";

std::vector<std::int8_t> bytes_of(const std::string& list) {
    std::vector<std::int8_t> out;
    const char* p = list.c_str();
    while (*p != '\0') {
        char* end = nullptr;
        out.push_back(static_cast<std::int8_t>(std::strtol(p, &end, 10)));
        p = *end == ',' ? end + 1 : end;
    }
    return out;
}

// Up to six reads of a stream, ending at the end mark. A range mark at the very start continues from the last
// model the reader gave, so the recorded runs keep one reader.
std::vector<std::int8_t> read_six(BoneModelStream& s, const std::vector<std::int8_t>& stream) {
    std::vector<std::int8_t> out;
    s.reset(stream.data());
    for (int step = 0; step < 6; ++step) {
        out.push_back(s.next());
        if (out.back() == kBoneStreamEnd) break;
    }
    return out;
}

std::vector<std::int8_t> read_six(const std::vector<std::int8_t>& stream) {
    BoneModelStream s;
    return read_six(s, stream);
}

}  // namespace

SH_TEST(bone_stream_as_recorded) {
    const auto rows = sh::test::golden_rows(kTable, "stream");
    SH_CHECK(rows.size() == 1500);
    int differences = 0;
    BoneModelStream reader;
    for (const auto& r : rows) {
        const std::vector<std::int8_t> read = read_six(reader, bytes_of(r.str(0)));
        if (read != bytes_of(r.str(1)) && ++differences <= 3) {
            std::string got;
            for (const std::int8_t v : read) got += (got.empty() ? "" : ",") + std::to_string(v);
            sh::test::golden_mismatch(kTable, r, got);
        }
    }
    SH_CHECK(differences == 0);
}

SH_TEST(bone_stream_seeded_run_digest) {
    const auto rows = sh::test::golden_rows(kTable, "digest");
    SH_CHECK(rows.size() == 1);
    for (const auto& r : rows) {
        sh::test::Sequence q(static_cast<std::uint64_t>(r.num(0)));
        sh::test::Digest d;
        BoneModelStream reader;
        for (std::int64_t it = 0; it < r.num(1); ++it) {
            std::vector<std::int8_t> s;
            const int len = 1 + q.below(8);
            for (int i = 0; i < len; ++i) {
                if (q.below(4) == 0) {
                    s.push_back(kBoneStreamMulti);
                    s.push_back(static_cast<std::int8_t>(q.below(12)));
                } else {
                    s.push_back(static_cast<std::int8_t>(q.below(40) - 3));
                }
            }
            s.insert(s.end(), 3, kBoneStreamEnd);
            for (const std::int8_t v : read_six(reader, s)) d.add(v);
        }
        SH_CHECK(sh::test::golden_digest_is(kTable, r, 2, d));
    }
}

// Plain bytes are model numbers, read one by one up to the end mark.
SH_TEST(bone_stream_reads_single_models) {
    SH_CHECK((read_six({4, 0, 9, kBoneStreamEnd}) == std::vector<std::int8_t>{4, 0, 9, kBoneStreamEnd}));
}

// The range mark continues from the model before it, one model a read, and the byte after the mark is the first
// model past the range; the stream goes on from that byte.
SH_TEST(bone_stream_expands_a_range) {
    SH_CHECK((read_six({2, kBoneStreamMulti, 6, kBoneStreamEnd}) == std::vector<std::int8_t>{2, 3, 4, 5, 6, kBoneStreamEnd}));
    // A range that is already at its end yields one model and moves on.
    SH_CHECK((read_six({7, kBoneStreamMulti, 3, 1, kBoneStreamEnd}) == std::vector<std::int8_t>{7, 8, 3, 1, kBoneStreamEnd}));
}

SH_TEST(bone_link_order_groups_by_model_order) {
    const std::uint8_t order[] = {2, 0, 1};
    const std::int32_t bones[] = {0, 1, 2, 2, 0, 5};
    std::uint32_t out[6];
    const std::size_t n = bone_link_order(order, 3, bones, 6, out);
    SH_CHECK(n == 5);
    const std::uint32_t want[] = {2, 3, 0, 4, 1};
    for (int i = 0; i < 5; i++) SH_CHECK(out[i] == want[i]);
}
