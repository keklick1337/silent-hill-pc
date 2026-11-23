// The exact twins of the game's matrices (engine/math/precise_capture.h): twins by address and what makes them
// stale, the twin of the loaded view, the camera and the matrices derived from it, the end of the frame.

#include "framework/sh_test.h"
#include "engine/math/geometry_state.h"
#include "engine/math/precise_capture.h"
#include "render/gpu/render_state.h"

#include <cmath>

namespace {

namespace capture = sh::math::capture;
using sh::math::Matrix3;
using sh::math::Transform;
using sh::math::Vec3i;

// Precise vertices on for one test, a fresh frame, no camera; off again at the end.
struct PreciseOn {
    PreciseOn() {
        sh::render::precise().enabled = 1;
        capture::end_frame();
        capture::clear_camera();
    }
    ~PreciseOn() {
        capture::clear_camera();
        capture::end_frame();
        sh::render::precise().enabled = 0;
    }
};

Matrix3 turn_about_y(double degrees) {
    const double a = degrees * 3.14159265358979323846 / 180.0;
    Matrix3 m;
    m.m[0][0] = static_cast<std::int16_t>(std::cos(a) * 4096.0);
    m.m[0][2] = static_cast<std::int16_t>(std::sin(a) * 4096.0);
    m.m[2][0] = static_cast<std::int16_t>(-std::sin(a) * 4096.0);
    m.m[2][2] = static_cast<std::int16_t>(std::cos(a) * 4096.0);
    return m;
}

bool same(const double* a, const double* b, int n) {
    for (int i = 0; i < n; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

// What the integers alone say.
void quantized(const Matrix3& m, double out[9]) {
    for (int i = 0; i < 9; ++i) out[i] = m.m[i / 3][i % 3] / 4096.0;
}

const double kExact[9] = {0.70710678, 0, 0.70710678, 0, 1, 0, -0.70710678, 0, 0.70710678};

}  // namespace

SH_TEST(precise_capture_off_finds_nothing) {
    sh::render::precise().enabled = 0;
    capture::clear_camera();
    Transform record;
    record.rot = turn_about_y(45);
    double out[9] = {};
    double t[3] = {};
    capture::note_rotation(&record, kExact);
    SH_CHECK(!capture::rotation_of(&record, out));
    SH_CHECK(!capture::translation_of(&record, t));
    capture::rotation_loaded(&record);
    SH_CHECK(!capture::current_rotation(out) && !capture::current_translation(t));
    // Switching on later does not bring the note back.
    sh::render::precise().enabled = 1;
    SH_CHECK(capture::rotation_of(&record, out));
    double plain[9];
    quantized(record.rot, plain);
    SH_CHECK(same(out, plain, 9));
    capture::end_frame();
    sh::render::precise().enabled = 0;
}

SH_TEST(precise_capture_twin_by_address) {
    PreciseOn on;
    Transform record;
    record.rot = turn_about_y(45);
    record.trans = {100, -200, 300};
    double out[9] = {};

    capture::note_rotation(&record, kExact);
    SH_CHECK(capture::rotation_of(&record, out) && same(out, kExact, 9));
    // Null arguments are ignored.
    capture::note_rotation(nullptr, kExact);
    capture::note_rotation(&record, nullptr);
    SH_CHECK(!capture::rotation_of(nullptr, out) && !capture::rotation_of(&record, nullptr));

    // The record's integers changed under the twin: it no longer stands for them.
    record.rot.m[0][0] += 1;
    double plain[9];
    quantized(record.rot, plain);
    SH_CHECK(capture::rotation_of(&record, out) && same(out, plain, 9));

    // A translation twin, and a translation nobody noted: whole units are exact as they are.
    const double exact_trans[3] = {100.25, -200.5, 300.75};
    double t[3] = {};
    SH_CHECK(capture::translation_of(&record, t) && t[0] == 100 && t[1] == -200 && t[2] == 300);
    capture::note_translation(&record, exact_trans);
    SH_CHECK(capture::translation_of(&record, t) && same(t, exact_trans, 3));
    record.trans.x = 101;
    SH_CHECK(capture::translation_of(&record, t) && t[0] == 101 && t[1] == -200);

    // Matrices whose elements are exact in Q12 need no note.
    Transform unit;
    SH_CHECK(capture::rotation_of(&unit, out) && out[0] == 1 && out[4] == 1 && out[8] == 1 && out[1] == 0);
    Transform squat;
    squat.rot.m[1][1] = 3072;
    SH_CHECK(capture::rotation_of(&squat, out) && out[4] == 0.75);
}

SH_TEST(precise_capture_forget_and_copy) {
    PreciseOn on;
    Transform a;
    a.rot = turn_about_y(45);
    double out[9] = {};
    double plain[9];
    quantized(a.rot, plain);

    capture::note_rotation(&a, kExact);
    capture::forget_rotation(&a);
    SH_CHECK(capture::rotation_of(&a, out) && same(out, plain, 9));

    // A product computed in one record and stored in another: the twin goes with it.
    Transform product;
    product.rot = a.rot;
    Transform kept;
    kept.rot = a.rot;
    capture::note_rotation(&product, kExact);
    capture::copy_rotation(&kept, &product);
    SH_CHECK(capture::rotation_of(&kept, out) && same(out, kExact, 9));
    // Copied from a record without a twin: the source's integers, which is all anyone knows.
    Transform bare;
    bare.rot = turn_about_y(30);
    Transform target;
    target.rot = bare.rot;
    capture::copy_rotation(&target, &bare);
    quantized(bare.rot, plain);
    SH_CHECK(capture::rotation_of(&target, out) && same(out, plain, 9));

    const double exact_trans[3] = {1.5, 2.5, 3.5};
    double t[3] = {};
    a.trans = {1, 2, 3};
    capture::note_translation(&a, exact_trans);
    capture::forget_translation(&a);
    SH_CHECK(capture::translation_of(&a, t) && t[0] == 1 && t[1] == 2 && t[2] == 3);
}

SH_TEST(precise_capture_frame_end_retires_the_twins) {
    PreciseOn on;
    Transform record;
    record.rot = turn_about_y(45);
    double out[9] = {};
    capture::note_rotation(&record, kExact);
    capture::end_frame();
    double plain[9];
    quantized(record.rot, plain);
    SH_CHECK(capture::rotation_of(&record, out) && same(out, plain, 9));

    // Many records in one frame keep their own twins.
    static Transform many[2000];
    for (int i = 0; i < 2000; ++i) {
        many[i].rot = turn_about_y(45);
        double exact[9];
        for (int k = 0; k < 9; ++k) exact[k] = kExact[k] + i * 1e-9;
        capture::note_rotation(&many[i], exact);
    }
    int right = 0;
    for (int i = 0; i < 2000; ++i) {
        if (capture::rotation_of(&many[i], out) && out[4] == kExact[4] + i * 1e-9) ++right;
    }
    SH_CHECK(right == 2000);
}

SH_TEST(precise_capture_loaded_view) {
    PreciseOn on;
    sh::math::GeometryState& g = sh::math::geometry();
    const Transform saved = g.view();
    Transform record;
    record.rot = turn_about_y(45);
    record.trans = {10, 20, 30};
    const double exact_trans[3] = {10.5, 20.5, 30.5};
    capture::note_rotation(&record, kExact);
    capture::note_translation(&record, exact_trans);
    double out[9] = {};
    double t[3] = {};

    // The geometry state's own load latches both.
    g.load_rotation(record.rot);
    g.load_translation(record);
    SH_CHECK(capture::current_rotation(out) && same(out, kExact, 9));
    SH_CHECK(capture::current_translation(t) && same(t, exact_trans, 3));

    // The view changed without a load: the latch stands for other integers.
    g.set_rotation(turn_about_y(10));
    g.set_translation({1, 2, 3});
    SH_CHECK(!capture::current_rotation(out) && !capture::current_translation(t));
    // A unit view is exact without a latch; a translation never is.
    g.set_rotation(Matrix3{});
    SH_CHECK(capture::current_rotation(out) && out[0] == 1 && out[4] == 1);

    // A record that does not hold the loaded integers latches nothing.
    g.set_rotation(turn_about_y(10));
    capture::rotation_loaded(&record);
    SH_CHECK(!capture::current_rotation(out));
    g.set_rotation(record.rot);
    capture::rotation_loaded(&record);
    SH_CHECK(capture::current_rotation(out) && same(out, kExact, 9));
    capture::rotation_loaded(nullptr);
    SH_CHECK(!capture::current_rotation(out));

    g.set_rotation(saved.rot);
    g.set_translation(saved.trans);
}

SH_TEST(precise_capture_camera_and_derived) {
    PreciseOn on;
    capture::CameraTwin twin;
    twin.key.rot = turn_about_y(45);
    twin.key.trans = {1000, 2000, 3000};
    for (int i = 0; i < 9; ++i) twin.rot[i] = kExact[i];
    twin.trans[0] = 1000.25;
    twin.trans[1] = 2000.5;
    twin.trans[2] = 3000.75;

    double out[9] = {};
    double t[3] = {};
    SH_CHECK(capture::camera() == nullptr && !capture::derived_rotation(twin.key.rot, out));
    capture::set_camera(twin);
    SH_CHECK(capture::camera() != nullptr);
    SH_CHECK(capture::camera_of(twin.key.rot, twin.key.trans) != nullptr);
    SH_CHECK(capture::camera_of(twin.key.rot, Vec3i{1000, 2000, 3001}) == nullptr);
    SH_CHECK(capture::derived_rotation(twin.key.rot, out) && same(out, kExact, 9));
    SH_CHECK(capture::derived_translation(twin.key.trans, t) && same(t, twin.trans, 3));
    SH_CHECK(!capture::derived_translation(Vec3i{5, 6, 7}, t));

    // A record that holds the camera's integers gets the camera's twin without a note.
    Transform record = twin.key;
    SH_CHECK(capture::rotation_of(&record, out) && same(out, kExact, 9));
    SH_CHECK(capture::translation_of(&record, t) && same(t, twin.trans, 3));
    // Unless it is marked as built from values nobody kept.
    Transform lossy = twin.key;
    capture::forget_rotation(&lossy);
    double plain[9];
    quantized(lossy.rot, plain);
    SH_CHECK(capture::rotation_of(&lossy, out) && same(out, plain, 9));

    // The camera moved to a point: the exact translation is the camera applied to the point.
    Transform moved = twin.key;
    moved.trans = {7, 8, 9};
    const double point[3] = {100, 0, 200};
    capture::register_camera_point(twin.key, point, moved);
    SH_CHECK(capture::derived_translation(moved.trans, t));
    SH_CHECK(t[0] == kExact[0] * 100 + kExact[2] * 200 + 1000.25);
    SH_CHECK(t[1] == 2000.5);
    SH_CHECK(t[2] == kExact[6] * 100 + kExact[8] * 200 + 3000.75);
    // From another matrix than the camera's nothing is noted.
    Transform elsewhere = twin.key;
    elsewhere.trans.x += 1;
    Transform result;
    result.trans = {70, 80, 90};
    capture::register_camera_point(elsewhere, point, result);
    SH_CHECK(!capture::derived_translation(result.trans, t));

    // The camera turned: the exact rotation is the camera's times the turn at unit scale.
    Transform turned;
    turned.rot = turn_about_y(60);
    Matrix3 unit;
    capture::register_camera_turn(twin.key, unit, turned);
    SH_CHECK(capture::derived_rotation(turned.rot, out) && same(out, kExact, 9));

    // Derived matrices retire with the frame; the camera stays until it is cleared.
    capture::end_frame();
    SH_CHECK(!capture::derived_translation(moved.trans, t) && !capture::derived_rotation(turned.rot, out));
    SH_CHECK(capture::derived_rotation(twin.key.rot, out));
    capture::clear_camera();
    SH_CHECK(!capture::derived_rotation(twin.key.rot, out) && capture::camera() == nullptr);
    // Without a camera nothing derived is noted.
    capture::register_derived(turned, kExact, nullptr);
    SH_CHECK(!capture::derived_rotation(turned.rot, out));
}
