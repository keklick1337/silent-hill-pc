// The work buffers (engine/memory/work_buffers.h): how much a read landing at an address may write.
#include "engine/memory/work_buffers.h"

#include "framework/sh_test.h"

#include <cstdint>

using sh::memory::WorkBuffer;

SH_TEST(work_buffers_room_ends_with_the_buffer) {
    sh::memory::WorkBuffers& buffers = sh::memory::work_buffers();
    // The demo's setup is the last sector of the block it shares with the map's data.
    std::uint8_t* const setup = buffers.as<std::uint8_t>(WorkBuffer::DemoSetup);
    SH_CHECK(buffers.room(setup) == 2048u);
    SH_CHECK(buffers.room(setup + 2047) == 1u);
    // A read that has filled the block comes here with its next sector: no room, and not "someone else's memory".
    SH_CHECK(buffers.room(setup + 2048) == 0u);
    int elsewhere = 0;
    SH_CHECK(!buffers.room(&elsewhere).has_value());
}
