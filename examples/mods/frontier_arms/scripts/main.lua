-- Frontier arms: logs the PC's item grants and quick keys through the hook bus (event catalog).

sh.hooks.on("item.given", function(ev)
    sh.log("frontier_arms: " .. ev.source .. " gives " .. ev.count .. " x " .. ev.item .. " on " .. ev.map)
    -- A revolver given without rounds comes with one cylinder.
    if ev.item == "frontier_arms:Revolver" and sh.item.count("frontier_arms:RevolverRounds") == 0 then
        sh.log("frontier_arms: the revolver comes without rounds; give revolverrounds")
    end
end, {id = "given"})

sh.hooks.on("player.quick_heal", function(ev)
    sh.log("frontier_arms: quick heal with " .. ev.item .. " for " .. (ev.amount // 4096) .. " health")
end, {id = "quick_heal"})

sh.hooks.on("game.quick_save", function(ev)
    sh.log("frontier_arms: quick save on " .. ev.map)
end, {id = "quick_save"})
