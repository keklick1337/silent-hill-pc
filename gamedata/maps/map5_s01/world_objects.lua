-- Set-up when the map loads (the end of Map_WorldObjectsInit): the npc flags id by difficulty.
do
    local difficulty = sh.save.game_difficulty()
    sh.game.set_npc_flags_id(difficulty == -1 and 3 or (difficulty == 0 and 4 or 5)) -- Easy 3, Normal 4, Hard 5
end
