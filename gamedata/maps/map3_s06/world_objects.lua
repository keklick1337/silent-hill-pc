-- Per-tick rule of map3_s06: once both the antique shop key and
-- the paper map are taken, M3S06_AntiqueShopKeyAndTownMapTaken gets set.
function update(dt)
    if sh.flag_get("M3S06_PickupAntiqueShopKey") and sh.flag_get("M2S00_PickupMap") then
        sh.flag_set("M3S06_AntiqueShopKeyAndTownMapTaken", true)
    end
end
