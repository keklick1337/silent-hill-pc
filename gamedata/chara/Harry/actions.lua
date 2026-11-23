-- Harry's action hooks (the section on action hooks). The game calls these when Harry
-- does something; each gets one table describing the event:
--   e.character "Harry", e.chara_id 1, e.slot (the sound slot of character.json),
--   e.sound (the sound name character.json chose, gamedata/sounds), e.id (its sound id),
--   e.surface (footsteps: the floor surface of gamedata/world/surfaces.json - "concrete",
--   "grass", "wood", "metal_grating", ...), e.place (the map's place rule that chose the
--   sound, maps/<map>/footsteps.json; nil when Harry's footsteps table did).
-- Return nil to keep that sound, or a sound name / id to play instead. A missing hook (or
-- this whole file missing) keeps the sounds of character.json.

-- Footstep sound per surface. Empty: every surface keeps the "footsteps" table of
-- character.json (a mod usually patches that table instead). This hook has the last word,
-- after the map's place rules; a mod overrides one surface with its own copy of this file, e.g.
--   grass = "footstep_concrete",
local footstep_by_surface = {
}

function on_footstep(e)
    return footstep_by_surface[e.surface]
end
