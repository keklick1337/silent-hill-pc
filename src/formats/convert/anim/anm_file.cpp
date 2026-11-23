#include "formats/convert/anim/anm_file.h"

#include <algorithm>

namespace sh::convert::anim {
namespace {

constexpr std::size_t kHeaderBytes = 0x14;
constexpr std::size_t kBoneBytes = 6;

std::uint16_t le16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}
void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

AnmHeader read_header(const std::uint8_t* d) {
    AnmHeader h;
    h.data_offset = le16(d);
    h.rotation_slots = d[2];
    h.translation_slots = d[3];
    h.keyframe_size = le16(d + 4);
    h.bone_count = d[6];
    h.pad7 = d[7];
    h.active_bones = le32(d + 8);
    h.file_size = le32(d + 12);
    h.keyframe_count = le16(d + 0x10);
    h.scale_log2 = d[0x12];
    h.root_y_offset = d[0x13];
    return h;
}

Result<void> check_header(const AnmHeader& h, std::size_t bones_in_table) {
    if (h.bone_count == 0 || h.keyframe_count == 0) {
        return Result<void>::fail("ANM: no bones or no keyframes");
    }
    if (h.rotation_slots * 9u + h.translation_slots * 3u != h.keyframe_size) {
        return Result<void>::fail("ANM: keyframe size does not match the slot counts");
    }
    if (h.data_offset < kHeaderBytes + h.bone_count * kBoneBytes) {
        return Result<void>::fail("ANM: data offset inside the bone table");
    }
    if (bones_in_table != h.bone_count) {
        return Result<void>::fail("ANM: bone table size differs from the header");
    }
    if (h.scale_log2 > 12) {
        return Result<void>::fail("ANM: scale above Q12");
    }
    return Result<void>::ok();
}

// `limit`: the header's keyframe count; the padding behind it is not keyframes even where it is long enough for one.
void read_keyframes(const AnmHeader& h, std::span<const std::uint8_t> data, std::size_t limit,
                    std::vector<AnmKeyframe>& keyframes, std::vector<std::uint8_t>& tail) {
    const std::size_t whole = std::min(limit, data.size() / h.keyframe_size);
    keyframes.reserve(whole);
    for (std::size_t k = 0; k < whole; ++k) {
        const std::uint8_t* p = data.data() + k * h.keyframe_size;
        AnmKeyframe kf;
        kf.translations.resize(h.translation_slots);
        kf.rotations.resize(h.rotation_slots);
        for (auto& t : kf.translations) {
            for (auto& v : t) {
                v = static_cast<std::int8_t>(*p++);
            }
        }
        for (auto& r : kf.rotations) {
            for (auto& v : r) {
                v = static_cast<std::int8_t>(*p++);
            }
        }
        keyframes.push_back(std::move(kf));
    }
    tail.assign(data.begin() + static_cast<std::ptrdiff_t>(whole * h.keyframe_size), data.end());
}

}  // namespace

bool AnmFile::has_header(std::span<const std::uint8_t> raw) {
    if (raw.size() < kHeaderBytes) {
        return false;
    }
    const AnmHeader h = read_header(raw.data());
    if (!check_header(h, h.bone_count)) {
        return false;
    }
    // fileSize is the padded size, so a few real files end 1-3 bytes before the nominal end of the last keyframe.
    return h.data_offset <= raw.size() &&
           h.data_offset + std::size_t{h.keyframe_size} * h.keyframe_count <= raw.size() + 4;
}

Result<AnmFile> AnmFile::parse(std::span<const std::uint8_t> raw) {
    using Out = Result<AnmFile>;
    if (raw.size() < kHeaderBytes) {
        return Out::fail("ANM: shorter than the header");
    }
    if (!has_header(raw)) {
        const AnmHeader h = read_header(raw.data());
        if (auto ok = check_header(h, h.bone_count); !ok) {
            return Out::fail(ok.error());
        }
        return Out::fail("ANM: keyframes extend past the end of the file");
    }
    AnmFile f;
    f.header_ = read_header(raw.data());
    const AnmHeader& h = f.header_;
    for (std::size_t i = 0; i < h.bone_count; ++i) {
        const std::uint8_t* b = raw.data() + kHeaderBytes + i * kBoneBytes;
        const auto s8 = [](std::uint8_t v) { return static_cast<int>(static_cast<std::int8_t>(v)); };
        f.bones_.push_back({s8(b[0]), s8(b[1]), s8(b[2]), {s8(b[3]), s8(b[4]), s8(b[5])}});
    }
    const std::size_t table_end = kHeaderBytes + h.bone_count * kBoneBytes;
    f.gap_.assign(raw.begin() + static_cast<std::ptrdiff_t>(table_end), raw.begin() + h.data_offset);
    read_keyframes(h, raw.subspan(h.data_offset), h.keyframe_count, f.keyframes_, f.tail_);
    return Out::ok(std::move(f));
}

Result<AnmFile> AnmFile::parse_extension(std::span<const std::uint8_t> raw, const AnmFile& base) {
    AnmFile f;
    f.extension_ = true;
    f.header_ = base.header_;
    f.bones_ = base.bones_;
    read_keyframes(f.header_, raw, raw.size(), f.keyframes_, f.tail_);
    if (f.keyframes_.empty()) {
        return Result<AnmFile>::fail("ANM: headerless file shorter than one keyframe of its base");
    }
    return Result<AnmFile>::ok(std::move(f));
}

Result<AnmFile> AnmFile::from_parts(bool extension, const AnmHeader& header, std::vector<AnmBone> bones,
                                    std::vector<std::uint8_t> gap, std::vector<AnmKeyframe> keyframes,
                                    std::vector<std::uint8_t> tail) {
    using Out = Result<AnmFile>;
    if (auto ok = check_header(header, bones.size()); !ok) {
        return Out::fail(ok.error());
    }
    if (!extension && kHeaderBytes + bones.size() * kBoneBytes + gap.size() != header.data_offset) {
        return Out::fail("ANM: bone table and gap do not end at the data offset");
    }
    if (!extension && keyframes.size() > header.keyframe_count) {
        return Out::fail("ANM: more keyframes than the header's count");
    }
    for (std::size_t k = 0; k < keyframes.size(); ++k) {
        if (keyframes[k].translations.size() != header.translation_slots ||
            keyframes[k].rotations.size() != header.rotation_slots) {
            return Out::fail("ANM: keyframe " + std::to_string(k) + " has the wrong number of slots");
        }
    }
    for (const AnmBone& b : bones) {
        for (const int v : {b.parent, b.rotation, b.translation, b.bind[0], b.bind[1], b.bind[2]}) {
            if (v < -128 || v > 127) {
                return Out::fail("ANM: bone value outside s8");
            }
        }
    }
    AnmFile f;
    f.extension_ = extension;
    f.header_ = header;
    f.bones_ = std::move(bones);
    f.gap_ = std::move(gap);
    f.keyframes_ = std::move(keyframes);
    f.tail_ = std::move(tail);
    return Out::ok(std::move(f));
}

std::vector<std::uint8_t> AnmFile::serialize() const {
    std::vector<std::uint8_t> out;
    if (!extension_) {
        const AnmHeader& h = header_;
        put16(out, h.data_offset);
        out.push_back(h.rotation_slots);
        out.push_back(h.translation_slots);
        put16(out, h.keyframe_size);
        out.push_back(h.bone_count);
        out.push_back(h.pad7);
        put32(out, h.active_bones);
        put32(out, h.file_size);
        put16(out, h.keyframe_count);
        out.push_back(h.scale_log2);
        out.push_back(h.root_y_offset);
        for (const AnmBone& b : bones_) {
            for (const int v : {b.parent, b.rotation, b.translation, b.bind[0], b.bind[1], b.bind[2]}) {
                out.push_back(static_cast<std::uint8_t>(static_cast<std::int8_t>(v)));
            }
        }
        out.insert(out.end(), gap_.begin(), gap_.end());
    }
    for (const AnmKeyframe& kf : keyframes_) {
        for (const auto& t : kf.translations) {
            for (const std::int8_t v : t) {
                out.push_back(static_cast<std::uint8_t>(v));
            }
        }
        for (const auto& r : kf.rotations) {
            for (const std::int8_t v : r) {
                out.push_back(static_cast<std::uint8_t>(v));
            }
        }
    }
    out.insert(out.end(), tail_.begin(), tail_.end());
    return out;
}

std::vector<Pose> AnmFile::local_poses(std::size_t k0, std::size_t k1, std::int32_t alpha) const {
    std::vector<Pose> out(bones_.size());
    if (keyframes_.empty()) {
        return out;
    }
    k0 = std::min(k0, keyframes_.size() - 1);
    k1 = std::min(k1, keyframes_.size() - 1);
    const AnmKeyframe& f0 = keyframes_[k0];
    const AnmKeyframe& f1 = keyframes_[k1];
    const int scale = header_.scale_log2;
    for (std::size_t i = 1; i < bones_.size(); ++i) {
        const AnmBone& b = bones_[i];
        Pose& p = out[i];
        if (b.translation < 0 || static_cast<std::size_t>(b.translation) >= f0.translations.size()) {
            for (std::size_t c = 0; c < 3; ++c) {
                p.t[c] = b.bind[c] * (1 << scale);
            }
        } else {
            const auto& a = f0.translations[static_cast<std::size_t>(b.translation)];
            const auto& z = f1.translations[static_cast<std::size_t>(b.translation)];
            for (std::size_t c = 0; c < 3; ++c) {
                p.t[c] = a[c] * (1 << scale) + (((z[c] - a[c]) * alpha) >> (12 - scale));
            }
            if (b.translation == 0) {
                p.t[1] -= header_.root_y_offset;
            }
        }
        if (b.rotation >= 0 && static_cast<std::size_t>(b.rotation) < f0.rotations.size()) {
            const auto& a = f0.rotations[static_cast<std::size_t>(b.rotation)];
            const auto& z = f1.rotations[static_cast<std::size_t>(b.rotation)];
            for (std::size_t c = 0; c < 9; ++c) {
                p.m[c] = a[c] * 32 + (((z[c] - a[c]) * alpha) >> 7);
            }
        }
    }
    return out;
}

std::vector<Pose> AnmFile::world_poses(const std::vector<Pose>& local) const {
    const std::size_t n = std::min(local.size(), bones_.size());
    std::vector<Pose> out(n);
    std::vector<std::uint8_t> state(n, 0);  // 0 unvisited, 1 in progress, 2 done
    // The table does not promise parents before children, so resolve recursively; depth is at most 255.
    struct Resolver {
        const std::vector<AnmBone>& bones;
        const std::vector<Pose>& local;
        std::vector<Pose>& out;
        std::vector<std::uint8_t>& state;
        void resolve(std::size_t i) {
            if (state[i] == 2) {
                return;
            }
            const int parent = bones[i].parent;
            if (state[i] == 1 || parent < 0 || static_cast<std::size_t>(parent) >= out.size() ||
                static_cast<std::size_t>(parent) == i) {
                out[i] = local[i];
                state[i] = 2;
                return;
            }
            state[i] = 1;
            resolve(static_cast<std::size_t>(parent));
            const Pose& pw = out[static_cast<std::size_t>(parent)];
            const Pose& l = local[i];
            Pose w;
            for (std::size_t r = 0; r < 3; ++r) {
                for (std::size_t c = 0; c < 3; ++c) {
                    std::int64_t sum = 0;
                    for (std::size_t k = 0; k < 3; ++k) {
                        sum += std::int64_t{pw.m[r * 3 + k]} * l.m[k * 3 + c];
                    }
                    w.m[r * 3 + c] = static_cast<std::int32_t>(sum >> 12);
                }
                std::int64_t sum = 0;
                for (std::size_t k = 0; k < 3; ++k) {
                    sum += std::int64_t{pw.m[r * 3 + k]} * l.t[k];
                }
                w.t[r] = static_cast<std::int32_t>((sum >> 12) + pw.t[r]);
            }
            out[i] = w;
            state[i] = 2;
        }
    };
    Resolver r{bones_, local, out, state};
    for (std::size_t i = 0; i < n; ++i) {
        r.resolve(i);
    }
    return out;
}

}  // namespace sh::convert::anim
