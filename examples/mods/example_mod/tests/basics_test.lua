-- Run from the modding tools: Lua editor > Tests > example_mod > Run tests (or the console: luaedit test example_mod).
-- Every global function named test_* is a test, run in name order after the ones given to test(name, fn).

function test_flag_round_trip()
    local before = sh.flag_get("M0S00_CherylFmvSeen")
    sh.flag_set("M0S00_CherylFmvSeen", not before)
    expect_eq(sh.flag_get("M0S00_CherylFmvSeen"), not before, "the flag follows flag_set")
    sh.flag_set("M0S00_CherylFmvSeen", before)
end

function test_unknown_flag_is_an_error()
    expect_error(function() sh.flag_get("example_mod:NoSuchFlag") end)
end

test("Q12 arithmetic", function()
    expect_eq(math.floor(1.5 * 4096), 6144)
    expect(4096 // 2 == 2048, "integer division")
end)
