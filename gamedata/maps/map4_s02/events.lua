-- map4_s02: Central Silent Hill Otherworld - streets
-- Event scripts of this area: the engine runs a handler every tick while its event is active.

function MapEvent_DoorJammed()
    sh.event_msg_with_sfx("common.lock_jammed", "door_jammed")
end

function MapEvent_DoorLocked()
    sh.event_msg_with_sfx("common.locked", "door_locked")
end

function MapEvent_Nop()
end
