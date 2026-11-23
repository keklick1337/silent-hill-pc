-- Stalker, Grey Child and Mumbler: one behaviour (map0_s00 alley, map1_s00..s03, map5_s02, map6_s03,
-- map6_s04, map7_s02). They wander, notice Harry by sight or by the falloff of his distance, chase the
-- point his last few steps predict, lunge at him or grab him from the front or behind, stagger and fall
-- under damage, get up again, and die where they fell. Values are Q12 (4096 = 1.0, angles 4096 = turn).

-- Collision shapes per keyframe (box top, bottom, height, offset y, cylinder radius, extent, box offset
-- x, z, cylinder offset x, z), one list in the order the game keeps them: a few lookups run past their group.
local KF = {
    { -2785, 0, 0, 0, 614, 0, 0, 0, -81, -450 },
    { -737, 0, 0, -450, 1146, 655, -81, -696, -40, -163 },
    { -737, 0, 0, -450, 1146, 655, -81, -696, -40, -163 },
    { -778, 0, 0, -491, 1146, 655, -81, -696, -40, -163 },
    { -819, 0, 0, -491, 1146, 655, -81, -696, -40, -163 },
    { -819, 0, 0, -491, 1146, 655, -81, -696, -40, -163 },
    { -819, 0, 0, -491, 1146, 655, -81, -696, -40, -163 },
    { -778, 0, 0, -491, 1146, 655, -81, -696, -40, -163 },
    { -778, 0, 0, -491, 1146, 655, -81, -696, -40, -163 },
    { -737, 0, 0, -450, 1146, 655, -81, -696, -40, -163 },
    { -696, 0, 0, -614, 1146, 573, 204, 737, 122, 286 },
    { -819, 0, 0, -737, 1146, 573, 204, 737, 122, 286 },
    { -901, 0, 0, -819, 1146, 573, 204, 737, 122, 286 },
    { -901, 0, 0, -819, 1146, 573, 204, 737, 122, 286 },
    { -819, 0, 0, -737, 1146, 573, 204, 737, 122, 286 },
    { -737, 0, 0, -655, 1146, 573, 204, 737, 122, 286 },
    { -737, 0, 0, -655, 1146, 573, 204, 737, 122, 286 },
    { -737, 0, 0, -614, 1146, 573, 40, 1597, 122, 1105 },
    { -778, 0, 0, -655, 1146, 573, 40, 1597, 122, 1105 },
    { -819, 0, 0, -696, 1146, 573, 40, 1597, 122, 1105 },
    { -778, 0, 0, -655, 1146, 573, 40, 1597, 122, 1105 },
    { -778, 0, 0, -655, 1146, 573, 40, 1597, 122, 1105 },
    { -737, 0, 0, -614, 1146, 573, 40, 1597, 122, 1105 },
    { -737, 0, 0, -614, 1146, 573, 40, 1597, 122, 1105 },
    { -4300, 0, -3481, -4014, 655, 327, 245, 696, 81, 409 },
    { -4423, 0, -3399, -4136, 655, 327, 122, -81, 81, -122 },
    { -4259, 0, -3072, -3973, 655, 327, 40, -778, 81, -450 },
    { -4259, 0, -3072, -3973, 655, 327, 40, -942, 40, -573 },
    { -4259, 0, -3072, -3973, 655, 327, 40, -860, 40, -532 },
    { -4300, 0, -3072, -3973, 655, 327, 81, -573, 40, -286 },
    { -4300, 0, -3072, -4055, 655, 327, 81, -368, 40, -163 },
    { -4177, 0, -3072, -3973, 655, 327, 122, -122, 81, -81 },
    { -4055, 0, -3031, -3850, 655, 327, 122, 40, 40, 81 },
    { -3973, 0, -3072, -3809, 655, 327, 163, 368, 81, 204 },
    { -3850, 0, -3072, -3604, 655, 327, 163, 532, 81, 327 },
    { -3850, 0, -3072, -3604, 655, 327, 163, 696, 81, 368 },
    { -3891, 0, -3072, -3604, 655, 327, 163, 696, 81, 368 },
    { -4014, 0, -3072, -3727, 655, 327, 163, 696, 81, 368 },
    { -4136, 0, -3153, -3850, 655, 327, 163, 614, 81, 368 },
    { -4259, 0, -3276, -3973, 655, 327, 204, 573, 81, 368 },
    { -4341, 0, -3317, -4096, 655, 327, 204, 573, 81, 368 },
    { -4300, 0, -3317, -4096, 655, 327, 163, 573, 81, 368 },
    { -4177, 0, -3194, -3973, 655, 327, 122, 655, 81, 368 },
    { -4136, 0, -3194, -3932, 655, 327, 40, 778, 40, 409 },
    { -4055, 0, -3194, -3891, 655, 327, 40, 737, 40, 450 },
    { -4055, 0, -3276, -3891, 655, 327, 40, 696, 0, 450 },
    { -4177, 0, -3399, -3891, 655, 327, 122, 573, 81, 368 },
    { -4300, 0, -3522, -4014, 655, 327, 204, 491, 81, 368 },
    { -4300, 0, -3522, -4096, 655, 327, 204, 491, 81, 327 },
    { -4300, 0, -3522, -4096, 655, 327, 204, 491, 81, 327 },
    { -4259, 0, -3522, -4055, 655, 327, 204, 491, 40, 327 },
    { -4259, 0, -3522, -4055, 655, 327, 163, 491, 40, 368 },
    { -4259, 0, -3399, -4055, 655, 327, 163, 491, 40, 368 },
    { -4259, 0, -3317, -4055, 655, 327, 163, 532, 40, 368 },
    { -4259, 0, -3317, -4055, 655, 327, 204, 532, 40, 368 },
    { -4259, 0, -3317, -4055, 655, 327, 204, 532, 40, 368 },
    { -2129, 0, -1187, -1925, 655, 327, -122, 573, -40, 204 },
    { -2252, 0, -1187, -1925, 655, 327, -163, 573, -40, 204 },
    { -2744, 0, -1515, -2334, 655, 327, -204, 573, -40, 204 },
    { -3276, 0, -1720, -2949, 655, 327, -245, 532, -40, 204 },
    { -3276, 0, -1884, -2949, 696, 327, -245, 696, -40, 327 },
    { -3072, 0, -1884, -2826, 737, 327, -204, 901, -40, 450 },
    { -2498, 0, -1392, -2170, 819, 368, -204, 901, -40, 409 },
    { -1556, 0, -778, -1310, 901, 450, -81, 1024, -40, 491 },
    { -819, 0, 0, -614, 983, 532, 0, 1269, 40, 696 },
    { -737, 0, 0, -614, 1064, 573, 40, 1597, 81, 1024 },
    { -737, 0, 0, -614, 1146, 573, 40, 1597, 122, 1105 },
    { -4300, 0, -3481, -4014, 655, 327, 245, 696, 81, 409 },
    { -4382, 0, -3358, -4136, 655, 327, 163, 409, 81, 245 },
    { -4382, 0, -3153, -4136, 655, 327, 81, -163, 81, 40 },
    { -3932, 0, -2539, -3563, 737, 327, 81, -573, 40, -163 },
    { -3276, 0, -1761, -2744, 819, 327, 40, -614, 40, -163 },
    { -2375, 0, -1351, -2007, 901, 409, 40, -614, 40, -122 },
    { -1884, 0, -901, -1556, 983, 409, 40, -655, 40, -122 },
    { -1351, 0, -450, -1146, 1064, 491, 0, -778, 40, -204 },
    { -942, 0, -204, -778, 1146, 573, 0, -778, 40, -204 },
    { -860, 0, -81, -655, 1146, 573, -40, -737, 40, -204 },
    { -778, 0, 0, -573, 1146, 655, -40, -655, 40, -204 },
    { -696, 0, 0, -491, 1146, 655, -40, -655, 0, -163 },
    { -737, 0, 0, -450, 1146, 655, -81, -614, -40, -163 },
    { -4300, 0, -3481, -4055, 655, 327, 245, 696, 122, 409 },
    { -3440, 0, -2088, -3072, 655, 327, 204, 573, 122, 409 },
    { -2498, 0, -1351, -2129, 737, 327, 163, 655, 122, 450 },
    { -1925, 0, -942, -1761, 819, 409, 204, 942, 163, 532 },
    { -1474, 0, -655, -1228, 901, 491, 204, 942, 163, 532 },
    { -1064, 0, -245, -860, 983, 573, 204, 819, 163, 368 },
    { -901, 0, -245, -696, 1064, 573, 204, 737, 163, 286 },
    { -860, 0, 0, -696, 1146, 573, 204, 737, 163, 286 },
    { -860, 0, 0, -696, 1146, 573, 204, 737, 163, 286 },
    { -819, 0, 0, -655, 1146, 573, 204, 737, 163, 286 },
    { -696, 0, 0, -614, 1146, 573, 204, 737, 122, 286 },
    { -737, 0, 0, -450, 1146, 655, -81, -614, -40, -163 },
    { -696, 0, 0, -614, 1146, 573, 204, 737, 122, 286 },
    { -737, 0, 0, -614, 1146, 573, 40, 1597, 122, 1105 },
    { -4300, 0, -3481, -4055, 655, 327, 245, 696, 122, 409 },
    { -4300, 0, -3481, -4055, 655, 327, 204, 696, 122, 409 },
    { -4300, 0, -3481, -4055, 655, 327, 81, 696, 122, 409 },
    { -4341, 0, -3522, -4055, 655, 327, -81, 614, 81, 327 },
    { -4300, 0, -3481, -4055, 655, 327, -163, 491, 0, 245 },
    { -4300, 0, -3481, -4055, 655, 327, -163, 368, 0, 163 },
    { -4300, 0, -3481, -4055, 655, 327, -163, 368, 0, 163 },
    { -4341, 0, -3522, -4055, 655, 327, -40, 409, 81, 163 },
    { -4341, 0, -3522, -4055, 655, 327, 122, 450, 122, 163 },
    { -4341, 0, -3522, -4055, 655, 327, 327, 450, 122, 163 },
    { -4341, 0, -3522, -4055, 655, 327, 491, 450, 245, 163 },
    { -4341, 0, -3522, -4055, 655, 327, 573, 368, 245, 163 },
    { -4300, 0, -3481, -4055, 655, 327, 532, 450, 245, 204 },
    { -4300, 0, -3481, -4055, 655, 327, 450, 614, 245, 327 },
    { -4300, 0, -3481, -4055, 655, 327, 368, 696, 245, 368 },
    { -4300, 0, -3481, -4055, 655, 327, 245, 778, 163, 409 },
    { -4300, 0, -3481, -4055, 655, 327, 245, 696, 122, 409 },
    { -4300, 0, -3481, -4055, 655, 327, 204, 696, 122, 409 },
    { -4218, 0, -3358, -3973, 655, 327, 204, 532, 122, 286 },
    { -4014, 0, -2990, -3809, 655, 327, 163, 286, 122, 204 },
    { -3850, 0, -2785, -3604, 655, 327, 163, 163, 122, 40 },
    { -3768, 0, -2621, -3522, 655, 327, 163, 40, 122, -81 },
    { -3727, 0, -2621, -3522, 655, 327, 163, 40, 122, -81 },
    { -3645, 0, -2539, -3440, 655, 327, 122, 122, 81, -81 },
    { -3440, 0, -2170, -3112, 655, 327, 40, 286, 81, -40 },
    { -3153, 0, -1966, -2867, 655, 327, 40, 491, 40, 122 },
    { -3031, 0, -1720, -2785, 655, 327, -40, 532, 0, 163 },
    { -2867, 0, -1720, -2703, 655, 327, -40, 532, 0, 163 },
    { -2703, 0, -1638, -2580, 655, 327, -40, 532, 0, 163 },
    { -2580, 0, -1515, -2334, 655, 327, -40, 532, 0, 163 },
    { -2416, 0, -1433, -2211, 655, 327, -122, 573, 0, 163 },
    { -2129, 0, -1187, -1925, 655, 327, -122, 573, -40, 204 },
    { -4300, 0, -3481, -4014, 655, 327, 245, 696, 81, 409 },
    { -4300, 0, -3481, -4014, 655, 327, 204, 696, 81, 409 },
    { -4341, 0, -3481, -4055, 655, 327, 163, 573, 122, 245 },
    { -4464, 0, -3481, -4136, 655, 327, 81, 409, 81, 204 },
    { -4505, 0, -3481, -4136, 655, 327, 40, 163, 40, 81 },
    { -4587, 0, -2990, -4218, 655, 327, 0, 0, 0, -40 },
    { -4587, 0, -2990, -4259, 655, 327, -81, -81, 0, -81 },
    { -4587, 0, -2990, -4259, 655, 327, -81, -163, 0, -163 },
    { -4587, 0, -2990, -4259, 655, 327, -81, -163, 0, -163 },
    { -4382, 0, -2990, -4055, 655, 327, -204, 327, -40, 163 },
    { -3932, 0, -3235, -3604, 696, 327, -245, 901, -81, 409 },
    { -3481, 0, -2293, -3235, 696, 327, -81, 737, -81, 532 },
    { -3481, 0, -2293, -3235, 696, 327, -81, 737, -81, 532 },
    { -3481, 0, -2293, -3235, 696, 327, -40, 737, -81, 532 },
    { -3481, 0, -2293, -3235, 696, 327, 0, 737, -81, 532 },
    { -3481, 0, -2293, -3235, 696, 327, 40, 737, -81, 532 },
    { -3604, 0, -2375, -3358, 696, 327, 81, 655, 0, 450 },
    { -3727, 0, -2539, -3481, 696, 327, 81, 614, 0, 409 },
    { -4177, 0, -3194, -3850, 696, 327, 122, 737, 40, 409 },
    { -4259, 0, -3235, -3973, 655, 327, 204, 655, 81, 368 },
    { -737, 0, 0, -450, 1146, 655, -81, -614, -40, -163 },
    { -737, 0, 0, -532, 1146, 655, -81, -614, -40, -163 },
    { -737, 0, 0, -614, 1146, 655, -81, -614, -40, -163 },
    { -778, 0, 0, -696, 1146, 614, -40, -573, -40, -163 },
    { -1146, 0, 0, -737, 1146, 573, 0, -614, -40, -163 },
    { -1392, 0, -122, -1064, 1064, 532, 0, -614, 0, -40 },
    { -2252, 0, -122, -1843, 983, 491, 40, -245, 81, 163 },
    { -2498, 0, -122, -2088, 901, 450, 81, 81, 81, 286 },
    { -2375, 0, -245, -2088, 819, 409, 204, 327, 122, 409 },
    { -2211, 0, -245, -1925, 737, 409, 204, 450, 163, 491 },
    { -2334, 0, -614, -1925, 737, 409, 245, 614, 163, 655 },
    { -2744, 0, -819, -2498, 737, 409, 245, 614, 163, 696 },
    { -3317, 0, -1802, -2990, 655, 327, 163, 778, 163, 614 },
    { -3809, 0, -2375, -3481, 655, 327, 0, 614, 40, 409 },
    { -4096, 0, -2457, -3768, 655, 327, -40, 614, 40, 327 },
    { -4259, 0, -2621, -4014, 655, 327, 0, 573, 0, 327 },
    { -4341, 0, -2949, -4014, 655, 327, 40, 532, 40, 286 },
    { -4382, 0, -3072, -4177, 655, 327, 81, 532, 40, 286 },
    { -4382, 0, -3072, -4177, 655, 327, 163, 491, 40, 286 },
    { -4382, 0, -3194, -4177, 655, 327, 204, 532, 40, 286 },
    { -4341, 0, -3276, -4096, 655, 327, 204, 532, 81, 368 },
    { -4300, 0, -3399, -4055, 655, 327, 245, 614, 81, 368 },
    { -696, 0, 0, -614, 1146, 573, 204, 737, 122, 286 },
    { -696, 0, 0, -614, 1146, 573, 204, 737, 122, 286 },
    { -696, 0, 0, -614, 1146, 573, 204, 737, 122, 286 },
    { -737, 0, 0, -655, 1146, 573, 163, 737, 122, 368 },
    { -737, 0, 0, -655, 1146, 573, 163, 737, 122, 368 },
    { -737, 0, 0, -655, 1146, 573, 163, 737, 122, 368 },
    { -819, 0, -81, -655, 1146, 573, 163, 737, 122, 409 },
    { -942, 0, -204, -696, 1146, 573, 163, 737, 81, 409 },
    { -1024, 0, -327, -819, 1146, 573, 163, 737, 81, 409 },
    { -1187, 0, -491, -1024, 1146, 573, 163, 696, 81, 409 },
    { -1310, 0, -614, -1228, 1146, 573, 163, 696, 81, 409 },
    { -1474, 0, -696, -1269, 1146, 573, 163, 696, 81, 409 },
    { -1679, 0, -819, -1474, 1064, 491, 163, 696, 81, 450 },
    { -1843, 0, -901, -1597, 983, 409, 163, 614, 81, 327 },
    { -2088, 0, -1024, -1802, 901, 327, 204, 573, 81, 286 },
    { -2375, 0, -1269, -2170, 819, 327, 245, 532, 81, 286 },
    { -3072, 0, -1802, -2826, 737, 327, 122, 532, 40, 163 },
    { -3399, 0, -1761, -3072, 696, 327, 122, 327, 40, 163 },
    { -3932, 0, -2252, -3686, 655, 327, 122, 245, 81, 163 },
    { -4096, 0, -2375, -3768, 655, 327, 122, 122, 122, 122 },
    { -3850, 0, -2375, -3563, 655, 327, 40, 245, 122, 122 },
    { -3809, 0, -2375, -3563, 655, 327, 163, 204, 163, 81 },
    { -3891, 0, -2621, -3645, 655, 327, 204, 40, 163, 81 },
    { -4055, 0, -2744, -3768, 655, 327, 409, -81, 163, 81 },
    { -4055, 0, -2826, -3768, 655, 327, 450, -81, 163, 81 },
    { -4259, 0, -3031, -3973, 655, 327, 450, 81, 163, 122 },
    { -4341, 0, -3194, -4096, 655, 327, 368, 245, 122, 286 },
    { -4300, 0, -3399, -4055, 655, 327, 286, 573, 81, 368 },
    { -4300, 0, -3399, -4055, 655, 327, 245, 614, 81, 368 },
    { -737, 0, 0, -614, 1146, 573, 40, 1597, 122, 1105 },
    { -778, 0, 0, -614, 1146, 573, 40, 1597, 122, 1105 },
    { -983, 0, 0, -737, 1146, 573, 40, 1597, 122, 1105 },
    { -1146, 0, 0, -860, 1146, 573, 122, 1638, 122, 1105 },
    { -1351, 0, -163, -1105, 1064, 532, 122, 1597, 122, 1105 },
    { -1638, 0, -163, -1310, 983, 491, 122, 1474, 122, 1064 },
    { -1720, 0, -532, -1474, 983, 491, 163, 1556, 122, 1064 },
    { -1925, 0, -655, -1597, 942, 491, 163, 1515, 122, 1024 },
    { -2088, 0, -860, -1802, 901, 450, 163, 1392, 122, 901 },
    { -2211, 0, -942, -1884, 860, 450, 163, 1310, 163, 860 },
    { -2457, 0, -1146, -2211, 860, 450, 122, 1187, 122, 778 },
    { -2908, 0, -1843, -2580, 778, 409, 122, 942, 122, 737 },
    { -3358, 0, -2498, -3153, 696, 368, 122, 860, 81, 614 },
    { -3727, 0, -2621, -3481, 655, 327, 122, 696, 81, 491 },
    { -4096, 0, -2949, -3850, 655, 327, 122, 573, 81, 450 },
    { -4259, 0, -3072, -4014, 655, 327, 204, 573, 81, 450 },
    { -4300, 0, -3235, -4014, 655, 327, 204, 573, 81, 409 },
    { -4300, 0, -3399, -4055, 655, 327, 245, 614, 81, 368 },
    { -778, 0, 0, -450, 1146, 614, 0, -532, 0, -163 },
    { -778, 0, 0, -450, 1146, 614, -40, -573, -40, -163 },
    { -737, 0, 0, -450, 1146, 655, -81, -614, -40, -163 },
    { -737, 0, 0, -450, 1146, 655, -81, -614, -40, -163 },
    { -2416, 0, -532, -2007, 737, 368, 286, 573, 81, 655 },
    { -2539, 0, -450, -2170, 737, 368, 286, 450, 81, 655 },
    { -2744, 0, -409, -2498, 819, 368, 286, 40, 163, 450 },
    { -1884, 0, -737, -1556, 901, 409, 122, -368, 122, -81 },
    { -1351, 0, -409, -1105, 983, 450, 122, -614, 122, -204 },
    { -901, 0, -40, -655, 1064, 491, 122, -614, 40, -163 },
    { -696, 0, 0, -491, 1146, 532, 40, -614, 0, -245 },
    { -696, 0, 0, -491, 1146, 573, 40, -614, 0, -245 },
    { -778, 0, 0, -491, 1146, 614, 40, -614, 0, -245 },
    { -737, 0, 0, -491, 1146, 614, 0, -614, 0, -245 },
    { -737, 0, 0, -450, 1146, 614, 0, -614, 0, -204 },
    { -3481, 0, -1024, -2990, 860, 491, 81, -40, 40, -245 },
    { -1966, 0, -409, -1556, 1024, 573, 81, -491, 0, -40 },
    { -819, 0, 0, -532, 1146, 614, -40, -614, -40, -163 },
    { -737, 0, 0, -450, 1146, 655, -81, -614, -40, -163 },
    { -737, 0, 0, -450, 1146, 655, -81, -614, -40, -163 },
    { -1310, 0, -573, -1105, 1024, 532, 204, 737, 0, 368 },
    { -1105, 0, -409, -901, 1064, 573, 204, 778, 81, 409 },
    { -778, 0, 0, -532, 1105, 614, 204, 696, 81, 368 },
    { -737, 0, 0, -573, 1146, 614, 204, 696, 163, 368 },
    { -737, 0, 0, -573, 1146, 614, 204, 696, 163, 368 },
    { -3522, 0, -2211, -3153, 737, 368, 245, 409, 81, 163 },
    { -3604, 0, -2211, -3153, 737, 368, 245, 409, 81, 163 },
    { -3604, 0, -2211, -3153, 737, 368, 286, 409, 81, 163 },
    { -3072, 0, -1638, -2662, 819, 368, 245, 532, 81, 204 },
    { -2170, 0, -1187, -1884, 901, 409, 204, 614, 122, 245 },
    { -1597, 0, -860, -1392, 1024, 491, 163, 696, 122, 245 },
    { -1187, 0, -450, -942, 1146, 573, 204, 778, 122, 368 },
    { -860, 0, -122, -737, 1146, 655, 204, 778, 122, 450 },
    { -737, 0, 0, -614, 1146, 655, 204, 778, 122, 450 },
    { -4300, 0, -3481, -4014, 655, 327, 245, 696, 81, 409 },
    { -4300, 0, -3481, -4014, 655, 327, 204, 778, 81, 409 },
    { -4259, 0, -3440, -4014, 655, 327, 204, 819, 81, 450 },
    { -4259, 0, -3440, -4014, 655, 327, 122, 860, 40, 450 },
    { -4259, 0, -3440, -4014, 655, 327, 122, 860, 40, 450 },
    { -4300, 0, -3481, -4055, 655, 327, 163, 819, 40, 450 },
    { -4300, 0, -3481, -4055, 655, 327, 122, 819, 40, 450 },
    { -4300, 0, -3481, -4055, 655, 327, 40, 819, 0, 450 },
    { -4300, 0, -3481, -4055, 655, 327, 40, 819, 0, 450 },
    { -4300, 0, -3481, -4055, 655, 327, 0, 778, 0, 409 },
    { -4341, 0, -3522, -4055, 655, 327, -40, 778, -40, 409 },
    { -4341, 0, -3522, -4055, 655, 327, -81, 778, -40, 409 },
    { -4341, 0, -3522, -4055, 655, 327, -81, 778, -122, 409 },
    { -4341, 0, -3522, -4055, 655, 327, -163, 778, -122, 409 },
    { -4341, 0, -3522, -4055, 655, 327, -204, 778, -122, 409 },
    { -4300, 0, -3481, -4055, 655, 327, -245, 778, -122, 409 },
    { -4218, 0, -3440, -4014, 655, 327, -204, 819, -122, 450 },
    { -4218, 0, -3440, -4014, 655, 327, -122, 860, -122, 450 },
    { -4218, 0, -3440, -4014, 655, 327, -122, 860, -122, 450 },
    { -4218, 0, -3440, -4014, 655, 327, -81, 860, -81, 450 },
    { -4259, 0, -3481, -4055, 655, 327, -122, 819, -122, 409 },
    { -4259, 0, -3481, -4055, 655, 327, -81, 819, -122, 409 },
    { -4259, 0, -3481, -4055, 655, 327, -40, 778, -40, 409 },
    { -4300, 0, -3481, -4055, 655, 327, 0, 778, -40, 409 },
    { -4300, 0, -3481, -4055, 655, 327, 40, 737, 0, 409 },
    { -4341, 0, -3522, -4055, 655, 327, 40, 696, 40, 409 },
    { -4341, 0, -3522, -4055, 655, 327, 40, 696, 40, 368 },
    { -4341, 0, -3522, -4055, 655, 327, 81, 696, 40, 368 },
    { -4341, 0, -3522, -4055, 655, 327, 122, 696, 40, 368 },
    { -4300, 0, -3481, -4055, 655, 327, 204, 696, 40, 368 },
}
local KF_AT = {
    HOLD = 0, -- 1
    DOWN_A = 1, -- 9
    DOWN_B = 10, -- 7
    DOWN_C = 17, -- 7
    HIT_A = 24, -- 16
    HIT_B = 40, -- 5
    STAGGER = 45, -- 11
    THROWN = 56, -- 11
    RECOIL = 67, -- 13
    RECOVER = 80, -- 11
    LIE_A = 91, -- 1
    LIE_B = 92, -- 1
    LIE_C = 93, -- 1
    GET_UP = 94, -- 16
    GRAB = 110, -- 16
    LUNGE = 126, -- 42
    RISE_B = 168, -- 29
    RISE_C = 197, -- 18
    TURN_1 = 215, -- 3
    TURN_3 = 218, -- 12
    TURN_2 = 230, -- 5
    TURN_4 = 235, -- 3
    TURN_5_FROM = 238, -- 2
    TURN_5 = 240, -- 9
    WALK = 249, -- 30
}

-- The collision shapes of row `i` of group `name` (0-based, as the clip's keyframe offset).
local function kf(name, i)
    return KF[KF_AT[name] + i + 1]
end

-- Clip numbers by the character data's names ("anims"), in the game's clip order; S takes the game's clip
-- number and packs the data's (clips past the named ones keep their number).
local C = clip_numbers("Stalker", {
    "still", "turn_from_lunge_a", "turn_from_lunge_b", "turn_from_lunge_long", "turn_from_getup_a",
    "turn_from_getup_long", "corpse_pose", "lunge", "hold_back", "hold_front", "thrown", "rise_a", "rise_b",
    "rise_c", "scripted_death_start", "scripted_death_end", "unused_after_scripted_death", "lie_b", "lie_c",
    "down_b", "down_c", "down_a", "hit_a", "hit_b", "recoil", "recover", "lie_a", "get_up", "idle_loop",
    "unused_grab_pose", "chase_run", "down_hold_a", "down_hold_b", "down_hold_c", "grab_front",
})

local function S(clip, active)
    return (C[clip] or clip) * 2 + (active and 1 or 0)
end

local E = sh.entity

-- stalker_flags
local F_HUNTING = 0x1       -- chases Harry (else wanders)
local F_ALLEY = 0x2         -- the alley stalkers' chase (control 2)
local F_EAGER = 0x4         -- commits to attacks more often
local F_WANDER_TURN = 0x8   -- a new wander heading is set
local F_AVOIDING = 0x10     -- turning away from a wall
local F_HOLDING = 0x20      -- holds Harry
local F_DOWN = 0x40         -- knocked down
local F_RISING = 0x80       -- getting up
local F_AIRBORNE = 0x100    -- falling
local F_BLED = 0x200        -- the death's blood is spawned
local F_AWARE = 0x400       -- knows where Harry is
local F_IDLE = 0x800        -- idles in place
local F_WARP_TURN = 0x1000  -- off screen and far: turns at once and moves faster
local F_PUSHED = 0x2000     -- pushed by a hit, the push decays first
local F_DEAD = 0x4000       -- the death cry played

-- Character flags (record flags) and the group flags of the stalkers (group 3).
local CF_TARGETED = 0x2
local CF_HIT = 0x4
local CF_IGNORED = 0x8      -- on Harry: enemies ignore him
local CF_CAMERA = 0x10
local CF_NO_RADIO_STATIC = 0x100
local GROUP = 3
local G_GRAB = 0x1
local G_ATTACK = 0x2

local NO_VALUE = -1
local EASY = -1
local NORMAL = 0
local WEAPON_LUNGE = sh.entity.attack_number("StalkerLunge") -- the stalkers' rows of the attack table
local WEAPON_GRAB = sh.entity.attack_number("StalkerLegGrab")
local ATTACK_GRAB_HOLD = sh.entity.attack_number("StalkerLegGrabDrain")

-- Difficulty values, set at the start of every update.
local max_health, down_health, regen, stagger_damage = 0, 0, 0, 0

-- A faded-out corpse leaves the record at the end of the tick: the rest of the tick still poses it with
-- the type's bones and clips.
local vanish = false

local function map_in(list)
    local m = sh.map_name()
    for _, name in ipairs(list) do
        if m == name then
            return true
        end
    end
    return false
end

-- Maps whose copy of the code uses the wider dark-light radii (control 3).
local WIDE_MAPS = { "map1_s02", "map1_s03", "map5_s02", "map6_s03", "map6_s04", "map7_s02" }
-- The school maps: shorter sight in the dark.
local SCHOOL_MAPS = { "map1_s00", "map1_s01" }
-- Maps where the dead fade out instead of bleeding.
local FADE_MAPS = { "map5_s02", "map7_s02" }

local function dt()
    return sh.game.delta_time()
end

local function rng(lo, hi)
    return sh.rng.int(lo, hi)
end

-- Turn step limit with a minimum of 1 (the game's TIMESTEP_ANGLE).
local function turn_limit(denom, shift)
    return asr(idiv(dt(), denom), shift) + 1
end

local function keyframe(c)
    return asr(c.anim_time, 12)
end

local function keyframe_in(c, lo, hi)
    local k = asr(c.anim_time, 12)
    return k >= lo and k <= hi
end

local function set_flags(c, set, clear)
    c.stalker_flags = (c.stalker_flags | (set or 0)) & ~(clear or 0)
end

local function group_set(set, clear)
    E.set_sys("chara_group_flags", (E.sys("chara_group_flags", GROUP) | (set or 0)) & ~(clear or 0), GROUP)
end

local function angle_to_player(c, x, z)
    local p = record(0)
    x = x or p.pos_x
    z = z or p.pos_z
    return s16(angle_signed(sh.math.atan2(x - c.pos_x, z - c.pos_z) - c.rot_y))
end

local function distance_to(c, x, z)
    return distance_2d(x - c.pos_x, z - c.pos_z)
end

local function abs(x)
    return x < 0 and -x or x
end

-- Sight of Harry (mode 0 by sight cone, 1 by proximity).
local function visible(c, mode, a, b)
    return E.player_visible(c.slot, mode, a, b) ~= 0
end

-- Something stands between the stalker and Harry.
local function blocked(c)
    return E.player_blocked(c.slot) ~= 0
end

local function on_screen(x0, x1, y0, y1, z0, z1)
    return E.aabb_visible(x0, x1, y0, y1, z0, z1) ~= 0
end

local function track(name, i)
    return E.stalker_track(name, i or 0)
end

local function track_set(name, i, v)
    E.set_stalker_track(name, i, v)
end

-- Sounds: the request kinds and their slots; the record keeps the id of the one requested last.
local function sfx_ids(c)
    return sound(c, "damage", 1365), sound(c, "attack", 1364), sound(c, "alert", 1363), sound(c, "attack_hit", 1366)
end

local function cry(c, kind)
    local hurt, lunge, alert, grab = sfx_ids(c)
    local id = NO_VALUE
    if kind == "hurt" then
        if c.stalker_sfx_priority < 3 then
            id = hurt
            c.stalker_sfx_priority = 3
            c.stalker_sfx_timer = rng(0, 818)
        end
    elseif c.stalker_sfx_priority < 2 then
        id = ({ lunge = lunge, alert = alert, grab = grab })[kind]
        c.stalker_sfx_priority = 2
        c.stalker_sfx_timer = rng(0, 1227)
    end
    if id ~= NO_VALUE then
        sh.sound.stop(c.stalker_sfx & 0xFFFF)
        c.stalker_sfx = id
    end
end

local function sfx_update(c)
    if c.stalker_sfx_priority == 0 then
        if c.stalker_flags & F_AWARE == 0 and c.stalker_flags & F_DOWN == 0 and c.control_state ~= 12 then
            c.stalker_sfx_timer = c.stalker_sfx_timer + dt()
            sh.rng.rand16()
        end
        return
    end
    local hurt, lunge, alert, grab = sfx_ids(c)
    local id = c.stalker_sfx
    if id == hurt then
        local t = c.stalker_sfx_timer
        if t > 819 and t < 4915 then
            E.sfx_3d(c.slot, hurt, 128, 0)
            c.stalker_sfx_timer = c.stalker_sfx_timer + 4096
        elseif t < 0 or t > 7372 then
            c.stalker_sfx_priority = 0
        end
        c.stalker_sfx_timer = c.stalker_sfx_timer + dt()
    elseif id == lunge then
        c.stalker_sfx_timer = c.stalker_sfx_timer + dt()
        local t = c.stalker_sfx_timer
        if t <= 2457 or t >= 6553 then
            if t < 0 or t > 11468 then
                c.stalker_sfx_priority = 0
            end
            return
        end
        E.sfx_3d(c.slot, id, 128, 0)
        c.stalker_sfx_timer = c.stalker_sfx_timer + 4096
    elseif id == alert then
        c.stalker_sfx_timer = c.stalker_sfx_timer + dt()
        local t = c.stalker_sfx_timer
        if t < 0 or t > 2048 then
            if rng(0, 7) ~= 0 then
                E.sfx_3d(c.slot, alert, 128, 0)
            end
            c.stalker_sfx_priority = 0
        end
    elseif id == grab then
        c.stalker_sfx_timer = c.stalker_sfx_timer + dt()
        local t = c.stalker_sfx_timer
        if t > 819 and t < 4915 then
            E.sfx_3d(c.slot, id, 128, 0)
            c.stalker_sfx_timer = c.stalker_sfx_timer + 4096
        elseif t < 0 or t > 12288 then
            c.stalker_sfx_priority = 0
        end
    end
end

-- The 30 fps attack decisions: at a higher frame rate only the frame that ends a 30 Hz game tick
-- commits, so the stalker decides (and rolls) as often per tick as at 30 fps, without a roll of its own.
local function may_commit()
    return sh.game.tick30() ~= 0
end

local function set_clip(c, status, kf_index)
    c.anim_status = status
    c.anim_time = kf_index << 12
    c.anim_keyframe = kf_index
end

local function init(c)
    c.stalker_flags = 0
    E.props_clear(c.slot)
    c.health = max_health
    if sh.map_name() == "map6_s03" and sh.save.game_difficulty() ~= EASY then
        c.health = q12_mul(max_health, 6144)
    end
    c.move_speed = 0
    c.fall_speed = 0
    c.rot_z = 0
    c.rot_x = 0
    c.anim_status = S(30, true)
    c.anim_time = 477 << 12
    c.anim_keyframe = 477
    c.heading_angle = c.rot_y

    local step = c.state_step
    if step == 11 or step == 3 or step == 8 then
        if step == 11 then
            set_flags(c, F_EAGER)
        end
        if step == 11 or step == 3 then
            c.stalker_timer = 10240
            set_flags(c, F_HUNTING)
        end
        c.control_state = 4
        E.set_collision_state(c.slot, 3)
    elseif step == 7 then
        c.control_state = 4
        set_flags(c, F_IDLE)
        E.set_collision_state(c.slot, 3)
    elseif step == 6 then
        c.control_state = 2
        c.flags = c.flags | CF_HIT
        c.stalker_timer = 4096
        set_flags(c, F_HUNTING | F_ALLEY)
        E.set_collision_state(c.slot, 3)
        c.flags = c.flags & ~CF_CAMERA
    elseif step == 5 then
        c.control_state = 1
        E.set_collision_state(c.slot, 0)
        c.flags = c.flags | CF_CAMERA
    elseif step == 9 then
        c.control_state = 12
        set_clip(c, S(27, true), 427)
        set_flags(c, F_HUNTING)
        E.set_collision_state(c.slot, 3)
    elseif step == 10 then
        c.control_state = 11
        c.anim_status = S(28, true)
        c.anim_time = 443 << 12
        c.anim_keyframe = 443
        E.set_collision_state(c.slot, 3)
    elseif step == 17 then
        c.control_state = 13
        set_clip(c, S(14, true), 176)
        E.set_collision_state(c.slot, 0)
    end

    c.state_step = 0
    c.stalker_sfx = NO_VALUE
    c.stalker_target_x = c.pos_x
    c.stalker_target_z = c.pos_z
    c.stalker_max_health = c.health
    E.clip_table_attach(c.slot, 0)
    damage_clear(c)
    if sh.map_name() == "map6_s03" then
        c.flags = c.flags | CF_NO_RADIO_STATIC
    end
end

-- A hit: the health drops, the stalker is pushed along the hit, and past the stagger threshold (or low
-- on health) it staggers or falls, the clip chosen by the pose it was hit in.
local function damage_update(c)
    c.stalker_stagger_damage = math.max(c.stalker_stagger_damage - q12_mul(dt(), 81920), 0)

    if c.damage_amount > 0 and c.health > 0 then
        cry(c, "hurt")
    end
    if c.anim_status == S(34, true) or c.anim_status == S(35, true) then
        c.damage_amount = 0
    end
    if c.health > 0 then
        c.health = math.min(c.health + q12_mul(dt(), regen), c.stalker_max_health)
    end
    if c.damage_amount <= 0 or c.health <= 0 then
        damage_clear(c)
        return
    end

    c.health = math.max(c.health - c.damage_amount, 0)
    c.stalker_max_health = c.stalker_max_health - asr(c.damage_amount, 2)
    set_flags(c, F_HUNTING | F_AWARE)
    c.damage_x = c.damage_x + asr(q12_mul32(c.move_speed, sh.math.sin(c.heading_angle)), 3)
    c.damage_z = c.damage_z + asr(q12_mul32(c.move_speed, sh.math.cos(c.heading_angle)), 3)
    local mag = sh.math.sqrt(s32(c.damage_x * c.damage_x + c.damage_z * c.damage_z))
    if c.stalker_flags & F_DOWN ~= 0 then
        c.move_speed = idiv(s32(mag << 12), 0xC000)
    else
        c.move_speed = idiv(s32(mag << 12), 6144)
    end
    c.heading_angle = sh.math.atan2(c.damage_x, c.damage_z)
    set_flags(c, F_PUSHED)
    c.stalker_stagger_damage = c.stalker_stagger_damage + c.damage_amount

    if c.stalker_stagger_damage >= stagger_damage or
        (c.stalker_flags & F_DOWN == 0 and c.health <= down_health) then
        c.stalker_stagger_damage = 0
        local p = record(0)
        local angle = abs(s16(angle_signed(sh.math.atan2(p.pos_x - c.pos_x, p.pos_z - c.pos_z) - c.rot_y)))
        local status = c.anim_status
        local k = keyframe(c)

        if c.stalker_flags & F_DOWN ~= 0 then
            if status == 53 then
                c.stalker_blend_from = NO_VALUE
                c.anim_status = 76
            elseif status == 35 then
                c.stalker_blend_from = NO_VALUE
                c.anim_status = 78
            elseif status == 37 then
                c.stalker_blend_from = NO_VALUE
                c.anim_status = 80
            end
        elseif c.health > down_health and c.stalker_flags & F_RISING == 0 then
            if c.control_state == 5 then
                group_set(0, G_ATTACK)
            end
            c.control_state = 7
            if (status >> 1) < 22 or (status >> 1) > 23 then
                c.anim_status = angle >= 682 and S(23, false) or S(22, false)
            end
        elseif c.stalker_flags & F_RISING ~= 0 then
            c.control_state = 9
            if (status >> 1) == 11 then
                if status == 22 then
                    c.stalker_blend_from = 0
                    c.anim_status = 76
                elseif k > 106 and k < 109 then
                    c.stalker_blend_from = k - 107
                    c.anim_status = 76
                elseif k > 108 and k < 113 then
                    c.anim_status = angle >= 682 and 84 or S(1, false)
                    c.stalker_blend_from = k - 107
                    local from = c.stalker_blend_from
                    if from == 2 then
                        c.stalker_blend_to = 2
                    elseif from == 3 then
                        c.stalker_blend_to = 1
                    else
                        c.stalker_blend_to = 0
                    end
                elseif k > 112 and k < 118 then
                    c.anim_status = angle >= 682 and 86 or S(3, false)
                    local ku = k & 0xFFFF
                    c.stalker_blend_from = ku - 107
                    if ku == 116 or ku == 117 then
                        c.stalker_blend_to = 1
                    else
                        c.stalker_blend_to = 2
                    end
                else
                    c.anim_status = angle >= 682 and 88 or S(2, false)
                    c.stalker_blend_from = k - 107
                end
            elseif (status >> 1) == 12 then
                if status == 24 then
                    c.stalker_blend_from = 0
                else
                    c.stalker_blend_from = k - 0x81
                end
                if status == 24 or (k > 128 and k < 136) then
                    c.anim_status = 78
                elseif k > 135 and k < 142 then
                    c.anim_status = S(4, false)
                    c.stalker_blend_to = asr(12 - c.stalker_blend_from, 1)
                elseif k > 141 and k < 149 then
                    c.anim_status = S(5, false)
                    local from = c.stalker_blend_from
                    if from == 13 then
                        c.stalker_blend_to = 5
                    elseif from == 14 or from == 15 then
                        c.stalker_blend_to = 4
                    elseif from == 16 or from == 17 then
                        c.stalker_blend_to = 3
                    else
                        c.stalker_blend_to = 2
                    end
                else
                    c.anim_status = S(5, false)
                    c.stalker_blend_to = 1
                end
            else
                if status == 26 then
                    c.stalker_blend_from = 0
                    c.anim_status = 80
                else
                    c.stalker_blend_from = k - 158
                    if k == 0x9E or k == 159 then
                        c.anim_status = 80
                    elseif k < 160 or k > 168 then
                        c.stalker_blend_to = 0
                        c.anim_status = S(5, false)
                    else
                        c.anim_status = 91
                    end
                end
            end
            set_flags(c, 0, F_RISING)
        else
            c.control_state = 9
            if angle < 682 then
                if k > 361 and k < 373 then
                    if k >= 364 and k <= 367 then
                        c.stalker_blend_to = 2
                    else
                        c.stalker_blend_to = 1
                    end
                    c.stalker_blend_from = k - 362
                else
                    c.stalker_blend_to = 0
                end
                c.anim_status = S(24, false)
            elseif status == 23 then
                c.stalker_blend_to = 23
                c.anim_status = S(25, false)
                c.stalker_blend_from = k - 107
            elseif status == 25 then
                c.stalker_blend_to = 25
                c.anim_status = S(25, false)
                c.stalker_blend_from = k - 129
            elseif status == 27 then
                c.stalker_blend_to = 27
                c.anim_status = S(25, false)
                c.stalker_blend_from = k - 158
            else
                c.stalker_blend_to = 83
                c.anim_status = S(41, false)
            end
        end

        c.stalker_timer = 0
        c.stalker_flags = c.stalker_flags & 0xF7E7
    end
    damage_clear(c)
end

-- Harry's speed and heading, sampled once per frame by the first stalker; after five samples the
-- stalkers chase where the averaged turn and speed say he is going.
local function track_update()
    local bgm = sh.sound.bgm_flags()
    if bgm & 0x10 ~= 0 then
        return
    end
    sh.sound.set_bgm_flags(bgm | 0x10)
    for i = 4, 1, -1 do
        track_set("speed", i, track("speed", i - 1))
        track_set("heading", i, track("heading", i - 1))
    end
    local p = record(0)
    local dx = s32(p.pos_x - track("prev_x"))
    local dz = s32(p.pos_z - track("prev_z"))
    local qx, qz = asr(dx, 6), asr(dz, 6)
    if dt() == 0 then
        track_set("speed", 0, 0)
    else
        track_set("speed", 0, idiv(s32(sh.math.sqrt(s32(qx * qx + qz * qz)) << 18), dt()))
    end
    track_set("heading", 0, sh.math.atan2(dx, dz))
    track_set("prev_x", 0, p.pos_x)
    track_set("prev_z", 0, p.pos_z)

    local samples = track("samples")
    if samples ~= 5 then
        track_set("samples", 0, samples + 1)
        track_set("predict_x", 0, p.pos_x)
        track_set("predict_z", 0, p.pos_z)
        return
    end
    local turn = 0
    for i = 0, 3 do
        turn = s32(turn + angle_signed(track("heading", i) - track("heading", i + 1)))
    end
    local heading = s16(track("heading", 0) + asr(turn, 2))
    local accel = 0
    for i = 0, 3 do
        accel = s32(accel + s32(track("speed", i) - track("speed", i + 1)))
    end
    local speed = s32(track("speed", 0) + asr(accel, 2))
    track_set("predict_x", 0, s32(asr(s32(speed * sh.math.sin(heading)), 14) + p.pos_x))
    track_set("predict_z", 0, s32(asr(s32(speed * sh.math.cos(heading)), 14) + p.pos_z))
end

-- Off screen and far away the stalker turns at once (and moves faster).
local function warp_if_unseen(c)
    if not on_screen(c.pos_x - 4096, c.pos_x + 4096, c.pos_y - 7372, c.pos_y, c.pos_z - 4096,
        c.pos_z - 4096) then
        set_flags(c, F_WARP_TURN)
    end
end

local function turn_by(c, delta, rate)
    if delta > 0 then
        c.rot_y = c.rot_y + q12_mul(dt(), rate)
    else
        c.rot_y = c.rot_y - q12_mul(dt(), rate)
    end
end

-- The lunge: the clip and the blend into it from the current walk keyframe.
local function lunge_start(c)
    c.control_state = 5
    group_set(G_ATTACK)
    cry(c, "lunge")
    if keyframe_in(c, 493, 504) then
        c.anim_status = S(37, true)
        c.stalker_blend_from = S(14, true)
        c.stalker_blend_to = 0
        return
    end
    c.anim_status = S(7, false)
    c.stalker_blend_from = keyframe(c) - 477
    if keyframe_in(c, 478, 481) then
        c.stalker_blend_to = 1
    elseif keyframe_in(c, 482, 487) then
        c.stalker_blend_to = 2
    elseif keyframe_in(c, 488, 490) then
        c.stalker_blend_to = 3
    elseif keyframe_in(c, 491, 492) then
        c.stalker_blend_to = 4
    else
        c.stalker_blend_to = 0
    end
end

-- The grab, from the front or behind by Harry's facing.
local function grab_start(c)
    c.control_state = 6
    local p = record(0)
    local delta = angle_signed(p.rot_y - c.rot_y)
    if abs(delta) > 1024 then
        c.anim_status = S(34, false)
    else
        c.anim_status = S(35, false)
    end
    c.stalker_blend_from = keyframe(c) - 477
    group_set(G_GRAB | G_ATTACK)
end

-- Control 2: the alley chase (map0_s00).
local function control_chase_alley(c)
    if sh.map_name() ~= "map0_s00" then
        return
    end
    local tx, tz = track("predict_x"), track("predict_z")
    local dist = distance_to(c, tx, tz)
    local delta = angle_to_player(c, tx, tz)

    if dist > 32768 and c.stalker_flags & F_HUNTING ~= 0 and sh.game.sys_flags() & 0x80 == 0 then
        warp_if_unseen(c)
    end

    local p = record(0)
    if c.anim_status == S(30, true) and not blocked(c) then
        c.stalker_timer = 0
        if c.stalker_flags & F_WARP_TURN ~= 0 then
            c.rot_y = sh.math.atan2(p.pos_x - c.pos_x, p.pos_z - c.pos_z)
        elseif abs(delta) > 1024 then
            speed_toward(c, 6144, 0)
        else
            speed_toward(c, 2048, (1024 - abs(delta)) * 4)
        end

        local i = 3 + (abs(delta) > 1024 and 1 or 0)
        while i > 0 do
            if turn_limit(1, 2) < abs(delta) then
                turn_by(c, delta, 2048)
                delta = angle_to_player(c, tx, tz)
            end
            if abs(delta) < turn_limit(1, 2) or dist > 8192 then
                break
            end
            i = i - 1
        end

        local commit = may_commit()
        local eager = c.stalker_flags & F_EAGER ~= 0
        if commit and (E.sys("chara_group_flags", GROUP) & G_GRAB ~= 0 or (eager and rng(0, 3) ~= 0) or
            (not eager and rng(0, 1) ~= 0)) then
            if dist < 3686 and (dist > 2867 or rng(0, 3) == 0) then
                if abs(delta) < 1024 and p.health > 0 then
                    lunge_start(c)
                    return
                end
            end
        elseif commit and dist > 2048 and dist < 4096 then
            if abs(delta) < 682 and p.health > 0 then
                grab_start(c)
                return
            end
        end

        if dist < 2867 then
            c.control_state = 4
        end
    else
        speed_toward(c, 2048, 4096)
        if c.stalker_timer == 0 then
            c.stalker_target_heading = E.heading_toward(c.slot, 8192, p.pos_x, p.pos_z, 4096, 1)
            if c.stalker_target_heading == 4096 then
                c.stalker_target_heading = c.rot_y - 2048
            end
        end
        c.stalker_timer = c.stalker_timer + dt()
        if c.stalker_timer > 10240 and rng(0, 15) == 0 then
            c.stalker_timer = 0
        end
        if c.stalker_flags & F_WARP_TURN ~= 0 then
            c.rot_y = c.stalker_target_heading
        else
            local d = angle_signed(c.stalker_target_heading - c.rot_y)
            if turn_limit(1, 2) < abs(d) then
                turn_by(c, d, 2048)
            end
        end
    end
end

-- The sight of a stalker by the map's light: 0 lit, 2 dark (flashlight), else unlit.
local function light_mode()
    return E.sys("map_effects") & 3
end

-- Control 3: the chase.
local function control_chase(c)
    if sh.map_name() == "map0_s00" then
        return
    end
    local tx, tz = track("predict_x"), track("predict_z")
    local dist = distance_to(c, tx, tz)
    local delta = angle_to_player(c, tx, tz)
    local wide = map_in(WIDE_MAPS)
    local radius_max, radius_min, seen
    local light = light_mode()
    if light == 0 then
        radius_max, radius_min = 61440, 22528
        seen = visible(c, 0, 30720, 30720)
    elseif light == 2 then
        if wide then
            radius_max, radius_min = 65536, 24576
            seen = visible(c, 0, 32768, 32768)
        else
            radius_max, radius_min = 49152, 18432
            seen = visible(c, 0, 24576, 24576)
        end
    else
        radius_max = 3276
        radius_min = wide and 4915 or 4096
        seen = visible(c, 1, 1638, 1638)
    end

    if seen or E.player_noise(c.slot, dist, 4096) ~= 0 then
        c.stalker_target_x = tx
        c.stalker_target_z = tz
        if c.stalker_flags & F_AWARE == 0 then
            set_flags(c, F_AWARE)
            cry(c, "alert")
        end
        c.stalker_lost_timer = 0
    elseif c.stalker_flags & F_AWARE ~= 0 then
        c.stalker_lost_timer = c.stalker_lost_timer + dt()
        if sh.rng.uint(0, 511) == 0 or c.stalker_lost_timer > 4915 then
            c.stalker_lost_timer = 0
            set_flags(c, 0, F_AWARE)
        end
    end

    if dist > 32768 and c.stalker_flags & F_HUNTING ~= 0 and c.stalker_flags & F_AWARE ~= 0 and
        sh.game.sys_flags() & 0x80 == 0 then
        warp_if_unseen(c)
    end

    local p = record(0)
    if c.anim_status == S(30, true) and c.stalker_flags & F_AIRBORNE == 0 and not blocked(c) then
        c.stalker_timer = 0
        if c.stalker_flags & F_PUSHED == 0 then
            speed_toward(c, 2048, (2048 - abs(delta)) * 2)
        end
        if c.stalker_flags & F_WARP_TURN ~= 0 then
            c.rot_y = sh.math.atan2(c.stalker_target_x - c.pos_x, c.stalker_target_z - c.pos_z)
        else
            local i = 3 + (abs(delta) > 1024 and 1 or 0)
            while i > 0 do
                if turn_limit(1, 3) < abs(delta) then
                    turn_by(c, delta, 1024)
                    delta = angle_to_player(c, tx, tz)
                end
                if abs(delta) < turn_limit(1, 3) or dist > asr(radius_min, 2) then
                    break
                end
                i = i - 1
            end
        end

        local commit = may_commit()
        local eager = c.stalker_flags & F_EAGER ~= 0
        local ignored = p.flags & CF_IGNORED ~= 0
        if commit and (E.sys("chara_group_flags", GROUP) & G_GRAB ~= 0 or E.any_npc_active() ~= 0 or
            abs(c.pos_y - p.pos_y) > 1228 or (eager and rng(0, 3) ~= 0) or (not eager and rng(0, 3) == 0)) then
            if not ignored and dist < 3686 and (dist > 2867 or rng(0, 3) == 0) and abs(delta) < 1024 and
                p.health > 0 then
                lunge_start(c)
            elseif dist < 2867 then
                c.control_state = 4
            end
        elseif commit and not ignored and dist < 4096 and dist > 2048 and abs(delta) < 682 and p.health > 0 then
            grab_start(c)
            c.attack_window = 1
        elseif dist < 2867 then
            c.control_state = 4
        end
    else
        if c.stalker_flags & F_PUSHED == 0 then
            speed_toward(c, 2048, 2867)
        end
        if c.stalker_timer == 0 then
            c.stalker_target_heading = E.heading_toward(c.slot, 10240, c.stalker_target_x, c.stalker_target_z, 4096, 1)
            if c.stalker_target_heading == 4096 then
                c.stalker_target_heading = c.rot_y - 2048
            end
        end
        c.stalker_timer = c.stalker_timer + dt()
        if c.stalker_timer > 12288 and rng(0, 15) == 0 then
            c.stalker_timer = 0
        end
        if c.stalker_flags & F_WARP_TURN ~= 0 then
            c.rot_y = c.stalker_target_heading
        else
            local d = s16(angle_signed(c.stalker_target_heading - c.rot_y))
            if asr(dt(), 3) + 1 < abs(d) then
                turn_by(c, d, 1024)
            end
        end
    end

    if c.anim_status == S(30, true) and radius_max * 2 < dist then
        local qx = asr(c.pos_x - c.stalker_target_x, 6)
        local qz = asr(c.pos_z - c.stalker_target_z, 6)
        local d = sh.math.sqrt(s32(qx * qx + qz * qz))
        if s32(d << 6) < asr(radius_min, 1) then
            c.control_state = 4
        end
    end
end

-- Control 4: wander (or idle) until Harry is noticed.
local function control_wander(c)
    local p = record(0)
    local dist = distance_to(c, p.pos_x, p.pos_z)
    local school = map_in(SCHOOL_MAPS)
    local near, seen
    local light = light_mode()
    if light == 0 then
        near = 22528
        seen = visible(c, 0, 30720, 30720)
    elseif light == 2 then
        if school then
            near = 18432
            seen = visible(c, 0, 49152, 0)
        else
            near = 24576
            seen = visible(c, 0, 65536, 0)
        end
    else
        near = school and 4096 or 4915
        seen = visible(c, 1, 1638, 1638)
    end
    local falloff = E.player_noise(c.slot, dist, 3276) ~= 0
    seen = seen or falloff

    if c.stalker_flags & F_PUSHED == 0 then
        if c.stalker_flags & F_IDLE ~= 0 then
            speed_toward(c, 2048, 1433)
        else
            speed_toward(c, 2048, 2867)
        end
    end

    local ox = c.pos_x - c.stalker_target_x
    local oz = c.pos_z - c.stalker_target_z
    local spread = math.max(abs(ox), abs(oz))

    if c.stalker_flags & 0x18 == 0 then
        local pick = false
        if not (rng(0, 511) ~= 0 and spread <= 16384) then
            pick = true
        elseif rng(0, 127) == 0 and sh.rng.uint(0, 4095) < idiv(s32(spread << 12), 16384) then
            pick = true
        end
        if pick then
            local range = c.stalker_flags & F_IDLE ~= 0 and 2048 or 16384
            local dx = rng(-range, range - 1) - ox
            local dz = rng(-range, range - 1) - oz
            c.stalker_target_heading = sh.math.atan2(dx, dz)
            set_flags(c, F_WANDER_TURN)
        end
    end

    if c.stalker_flags & F_AVOIDING == 0 and E.path_hit(c.slot, 4096, c.rot_y) ~= 0 then
        c.stalker_target_heading = E.clear_heading(c.slot, 14336, c.rot_y)
        if c.stalker_target_heading == 4096 then
            c.stalker_target_heading = c.rot_y + 2048
        end
        set_flags(c, F_AVOIDING)
    end

    local d = angle_signed(c.stalker_target_heading - c.rot_y)
    if turn_limit(1, 4) < abs(d) then
        turn_by(c, d, 512)
    else
        set_flags(c, 0, F_WANDER_TURN | F_AVOIDING)
    end

    if c.stalker_flags & F_IDLE ~= 0 then
        if rng(0, 1023) == 0 then
            set_flags(c, 0, F_WANDER_TURN)
        end
        if rng(0, 1023) == 0 then
            set_flags(c, 0, F_AVOIDING)
        end
        if dist < 8192 and dist < near and not blocked(c) then
            c.stalker_timer = 20480
            set_flags(c, F_HUNTING, F_IDLE)
        end
    end

    if c.stalker_flags & F_HUNTING ~= 0 and (c.stalker_flags & F_AWARE ~= 0 or seen) then
        if seen then
            c.stalker_target_x = p.pos_x
            c.stalker_target_z = p.pos_z
            c.stalker_lost_timer = 0
        end
        if c.stalker_flags & F_AWARE == 0 then
            set_flags(c, F_AWARE)
            cry(c, "alert")
        end
        local go = false
        if rng(0, 15) == 0 and c.stalker_timer > 4096 and c.stalker_flags & F_ALLEY ~= 0 then
            go = true
        elseif rng(0, 15) == 0 and c.stalker_timer > 10240 then
            go = true
        elseif rng(0, 31) == 0 and asr(near, 1) < dist then
            go = true
        end
        if go then
            c.control_state = c.stalker_flags & F_ALLEY ~= 0 and 2 or 3
            set_flags(c, 0, F_WANDER_TURN | F_AVOIDING)
        else
            c.stalker_timer = c.stalker_timer + dt()
        end
    end

    if not seen and c.stalker_flags & F_AWARE ~= 0 then
        c.stalker_lost_timer = c.stalker_lost_timer + dt()
        if rng(0, 4095) == 0 or c.stalker_lost_timer > 4915 then
            c.stalker_lost_timer = 0
            set_flags(c, 0, F_AWARE)
        end
    end
end

-- move_speed one step towards `limit` by `rate` per second, never past it (signed limits).
local function speed_towards(c, limit, rate)
    local step = q12_mul(dt(), rate)
    local ms = c.move_speed
    local v
    if ms > limit then
        v = ms - step
        if v < limit then
            v = limit
        end
    else
        v = limit
        local up = ms + step
        if up <= limit then
            v = up
        end
    end
    c.move_speed = v
end

-- Control 5: the lunge.
local function control_lunge(c)
    if (c.anim_status >> 1) == C[30] then
        c.control_state = 4
        group_set(0, G_ATTACK)
        return
    end
    if keyframe_in(c, 62, 64) then
        c.move_speed = 0
    elseif keyframe_in(c, 50, 61) then
        local p = record(0)
        local dist = distance_to(c, p.pos_x, p.pos_z)
        for _ = 0, 5 do
            local delta = angle_to_player(c)
            local limit = asr(idiv(dt(), 3), 3) + 1
            if (delta >= 0 and limit < delta) or (delta < 0 and limit < -delta) then
                if delta > 0 then
                    c.rot_y = q12_mul(dt(), 341) + c.rot_y
                else
                    c.rot_y = c.rot_y - q12_mul(dt(), 341)
                end
            end
        end
        if c.stalker_flags & F_PUSHED == 0 then
            if dist > 3686 then
                speed_towards(c, 2867, 4096)
                return
            end
            if dist < 2867 then
                speed_towards(c, -2867, 4096)
                return
            end
            speed_toward(c, 24576, 0)
        end
    else
        speed_toward(c, 24576, 0)
    end
end

-- Control 6: the grab, then the hold that hurts Harry until he breaks free.
local function control_grab(c)
    E.set_collision_state(c.slot, 3)
    speed_toward(c, 6144, 0)
    local p = record(0)
    local dist = distance_to(c, p.pos_x, p.pos_z)
    local step = 0
    local delta = angle_to_player(c)

    if abs(delta) < 512 then
        if c.anim_status == S(34, true) and dist > 2170 then
            step = s16(q12_mul(dt(), 4096))
        elseif c.anim_status == S(35, true) and dist > 1556 then
            step = s16(q12_mul(dt(), 4096))
        end
    end
    for _ = 0, 2 do
        if turn_limit(1, 4) < abs(delta) then
            turn_by(c, delta, 512)
        end
        delta = angle_to_player(c)
    end

    if c.stalker_flags & F_HOLDING ~= 0 then
        if c.anim_status == S(9, true) then
            if dist < 2129 or dist > 2211 then
                step = s16(math.min(q12_mul(dt(), 4096), dist - 2170))
            end
            c.stalker_hold_time = c.anim_time - (83 << 12)
        elseif c.anim_status == S(8, true) then
            if dist < 1515 or dist > 1597 then
                step = s16(math.min(q12_mul(dt(), 4096), dist - 1556))
            end
            c.stalker_hold_time = c.anim_time - (70 << 12)
        else
            c.stalker_hold_time = 0
        end
        c.rot_y = sh.math.atan2(p.pos_x - c.pos_x, p.pos_z - c.pos_z)

        if p.attack_received == NO_VALUE then
            group_set(0, G_GRAB)
            local status = c.anim_status
            c.control_state = 9
            c.anim_status = S(10, false)
            c.stalker_blend_from = status
            E.set_collision_state(c.slot, 0)
            E.set_collision_state(c.slot, 3)
            set_flags(c, F_DOWN, F_HOLDING)
        else
            c.stalker_timer = c.stalker_timer + dt()
            if c.stalker_timer > 6144 then
                local damage = idiv(s32((E.weapon_damage(ATTACK_GRAB_HOLD) << 12) * sh.rng.uint(85, 116)), 100)
                p.damage_amount = p.damage_amount + damage
                c.stalker_timer = sh.rng.rand16() % 2048
            end
        end
    elseif keyframe_in(c, 472, 476) then
        local hit = E.attack(c.slot, 1, WEAPON_GRAB, c.pos_x, c.pos_y + c.box_extent_x, c.pos_z, 0, c.rot_y, 1024)
        if hit ~= NO_VALUE then
            set_flags(c, F_HOLDING)
            group_set(0, G_ATTACK)
            cry(c, "grab")
            c.stalker_timer = 4096
        end
    elseif c.anim_status == S(9, true) or c.anim_status == S(8, true) or dist > 8192 then
        group_set(0, G_GRAB | G_ATTACK)
        c.control_state = 9
        local status = c.anim_status
        if status == S(34, true) or status == S(9, false) then
            c.stalker_blend_from = S(9, true)
        elseif status == S(35, true) or status == S(8, false) then
            c.stalker_blend_from = S(8, true)
        else
            c.stalker_blend_from = status
        end
        set_flags(c, F_DOWN)
        c.anim_status = S(36, false)
        E.set_collision_state(c.slot, 3)
    end

    if dt() ~= 0 then
        c.stalker_step_x = q12_mul32(step, sh.math.sin(c.rot_y))
        c.stalker_step_z = q12_mul32(step, sh.math.cos(c.rot_y))
    end
end

-- Back to the chase from getting up (clip 27 playing), now and then.
local function get_up_done(c, check_pushed)
    if c.anim_status == S(27, true) and rng(0, 3) == 0 then
        local flags = c.stalker_flags & 0xFFFF
        if not check_pushed or flags & F_PUSHED == 0 then
            c.control_state = flags & F_ALLEY ~= 0 and 2 or 3
            c.anim_status = S(30, false)
            c.stalker_blend_from = S(27, true)
            c.stalker_blend_to = keyframe(c) - 427
        end
    end
end

-- Control 7: staggered.
local function control_stagger(c)
    if c.stalker_flags & F_PUSHED == 0 then
        speed_toward(c, 6144, 0)
    end
    get_up_done(c, true)
end

-- Control 8: getting up.
local function control_rise(c)
    c.flags = c.flags & ~CF_TARGETED
    if c.stalker_flags & F_PUSHED == 0 then
        speed_toward(c, 6144, 0)
    end
    speed_toward(c, 6144, 0)

    local k = keyframe(c)
    local div, mult = 0, 0
    if k >= 110 and k < 117 then
        mult, div = 1228, 7
    elseif k >= 117 and k < 121 then
        mult, div = 1228, 4
    elseif k >= 121 and k < 124 then
        mult, div = 409, 4
    end
    if mult ~= 0 then
        local duration = E.clip_duration(c.slot, c.anim_status)
        local step = idiv(s32(q12_mul(duration, dt()) * mult), div << 12)
        c.stalker_step_x = q12_mul32(step, sh.math.sin(c.rot_y))
        c.stalker_step_z = q12_mul32(step, sh.math.cos(c.rot_y))
    end

    if c.stalker_flags & F_RISING ~= 0 then
        k = keyframe(c)
        if (k >= 121 and k < 129) or (k >= 149 and k < 158) or (k >= 171 and k < 176) then
            set_flags(c, 0, F_RISING)
        end
    end

    get_up_done(c, false)

    local light = light_mode()
    local seen
    if light == 0 then
        seen = visible(c, 0, 30720, 0)
    elseif light == 2 then
        seen = visible(c, 0, map_in(SCHOOL_MAPS) and 24576 or 32768, 0)
    else
        seen = visible(c, 1, 1638, 0)
    end
    if seen or E.player_noise(c.slot, 0, 4096) ~= 0 then
        local p = record(0)
        c.stalker_target_x = p.pos_x
        c.stalker_target_z = p.pos_z
    end
end

-- Control 9: down (knocked over, thrown off, falling); the push of the clip, then death or getting up.
local function control_down(c)
    if c.health < down_health then
        c.flags = c.flags | CF_TARGETED
    end
    set_flags(c, F_DOWN)
    if c.stalker_flags & F_PUSHED == 0 then
        speed_toward(c, 6144, 0)
    end

    local div, turn, dist = 0, 0, 0
    local status = c.anim_status
    if status == S(24, false) then
        dist, div, turn = 614, 1, 2048
    elseif keyframe_in(c, 395, 402) then
        dist, div, turn = 4096, 8 - c.stalker_blend_to, 2048
    elseif keyframe_in(c, 407, 412) then
        dist, div, turn = 2048, 5, 0
    elseif keyframe_in(c, 0, 3) then
        dist, div, turn = 409, 4, 2048
    elseif keyframe_in(c, 9, 17) then
        dist, div, turn = 1228, 9, 2048
    elseif keyframe_in(c, 4, 6) then
        dist, div, turn = 1638, 3, 2048
    elseif keyframe_in(c, 20, 23) then
        dist, div, turn = 409, 5, 0
    elseif keyframe_in(c, 27, 32) then
        dist, div, turn = 1228, 6, 0
    elseif status == S(45, true) and keyframe_in(c, 159, 167) then
        dist, div, turn = 1228, 8, 2048
    elseif status == S(10, true) and keyframe_in(c, 96, 100) then
        dist, div, turn = 819, 5, 2048
    end
    if dist ~= 0 then
        dist = idiv(s32(dist * q12_mul(E.clip_duration(c.slot, c.anim_status), dt())), div << 12)
        c.stalker_step_x = q12_mul32(dist, sh.math.sin(c.rot_y + turn))
        c.stalker_step_z = q12_mul32(dist, sh.math.cos(c.rot_y + turn))
    end

    status = c.anim_status
    if status == S(26, true) or status == S(17, true) or status == S(18, true) then
        if c.health == 0 and c.move_speed == 0 then
            if c.anim_status == S(26, true) then
                c.anim_status = S(21, false)
            end
            if c.anim_status == S(17, true) then
                c.anim_status = S(19, false)
            end
            if c.anim_status == S(18, true) then
                c.anim_status = S(20, false)
            end
            c.control_state = 10
            E.enemy_defeated(c.slot)
        end
        if c.health > down_health and rng(0, 7) == 0 and c.stalker_flags & F_PUSHED == 0 then
            if c.anim_status == S(26, true) then
                c.anim_status = S(11, false)
            end
            if c.anim_status == S(17, true) then
                c.anim_status = S(12, false)
            end
            if c.anim_status == S(18, true) then
                c.anim_status = S(13, false)
            end
            c.control_state = 8
            set_flags(c, F_RISING, F_DOWN)
        end
    end
end

-- Control 10: dead.
local function control_dead(c)
    if c.stalker_flags & F_PUSHED == 0 then
        local ms = c.move_speed
        local step = q12_mul(dt(), 6144)
        if ms > 0 then
            c.move_speed = math.max(ms - step, 0)
        else
            c.move_speed = math.min(ms + step, 0)
        end
    end
    if c.health == 0 then
        c.flags = c.flags & ~CF_TARGETED
        if E.sys("target_npc") ~= E.npc_index(c.slot) then
            c.health = NO_VALUE
            E.set_collision_state(c.slot, 0)
        end
    end
    local fade = map_in(FADE_MAPS)
    if c.move_speed == 0 and c.stalker_flags & (F_AIRBORNE | F_BLED) == 0 then
        local pattern = c.anim_status == S(31, true) and S(0, true) or S(0, false)
        if c.anim_status == S(32, true) then
            pattern = S(1, false)
        end
        if c.anim_status == S(33, true) then
            pattern = S(6, false)
        end
        if pattern ~= S(0, false) then
            if not fade then
                E.anim_status_set(c.slot, pattern, 3)
            end
            set_flags(c, F_BLED)
        end
    end
    if fade and c.stalker_flags & F_BLED ~= 0 then
        c.aux_timer = c.aux_timer + q12_mul(dt(), 1024)
        if c.aux_timer > 4096 then
            vanish = true
        end
    end
end

-- Control 12: crouching in wait until Harry comes close.
local function control_wait(c)
    local p = record(0)
    local dist = distance_to(c, p.pos_x, p.pos_z)
    local delta = angle_to_player(c)
    local light = light_mode()
    local seen
    if light == 0 then
        seen = visible(c, 0, 30720, 30720)
    elseif light == 2 then
        seen = visible(c, 0, map_in(SCHOOL_MAPS) and 49152 or 65536, 0)
    else
        seen = visible(c, 1, 1638, 3276)
    end
    local falloff = E.player_noise(c.slot, dist, 4096) ~= 0
    if not (seen or falloff) then
        return
    end
    c.stalker_target_x = p.pos_x
    c.stalker_target_z = p.pos_z
    if dist < 4915 and delta < 2048 then
        c.control_state = c.stalker_flags & F_ALLEY == 0 and 3 or 2
        c.anim_status = S(30, false)
        c.stalker_blend_from = 55
        c.stalker_blend_to = keyframe(c) - 427
        set_flags(c, F_AWARE)
        cry(c, "alert")
    end
end

-- Control 13: the scripted death.
local function control_scripted_death(c)
    if c.state_step == 1 then
        c.state_step = c.state_step + 1
        c.anim_status = S(15, false)
    end
    if c.stalker_flags & F_DEAD == 0 and c.anim_time > (211 << 12) then
        cry(c, "hurt")
        c.health = NO_VALUE
        set_flags(c, F_DEAD)
    end
    if c.state_step == 3 then
        c.state_step = c.state_step + 1
        set_clip(c, S(47, true), 294)
        if c.health ~= NO_VALUE then
            E.effect_spawn(2, 575037, -2252, 93224, 3, 3)
            c.health = NO_VALUE
            set_flags(c, F_DEAD)
        end
    end
end

local CONTROL = {
    [2] = control_chase_alley,
    [3] = control_chase,
    [4] = control_wander,
    [5] = control_lunge,
    [6] = control_grab,
    [7] = control_stagger,
    [8] = control_rise,
    [9] = control_down,
    [10] = control_dead,
    [12] = control_wait,
    [13] = control_scripted_death,
}

local function control_update(c)
    if c.stalker_flags & F_AWARE ~= 0 then
        c.flags = c.flags | CF_HIT
    end
    local status = c.anim_status
    if status ~= S(34, true) then
        local attacking = status > S(34, true) and S(35, true) or S(7, true)
        if status ~= attacking then
            c.attack_window = 0
        end
    end
    track_update()
    local fn = CONTROL[c.control_state]
    if fn ~= nil then
        fn(c)
    end
end

-- Gravity, the step of the tick (faster when warping), the fall flag.
local function move_update(c)
    c.fall_speed = c.fall_speed + sh.game.gravity_speed()
    local heading = angle_signed(c.rot_y)
    c.rot_y = heading
    if c.stalker_flags & F_PUSHED ~= 0 then
        speed_toward(c, 6144, 0)
        if c.move_speed == 0 then
            set_flags(c, 0, F_PUSHED)
        end
    else
        c.heading_angle = heading
    end
    local speed = c.move_speed
    if c.stalker_flags & F_WARP_TURN ~= 0 then
        if c.control_state == 2 then
            c.move_speed = s32(speed * 8)
        else
            c.move_speed = s32(speed * 2)
        end
    end
    if dt() ~= 0 then
        E.move(c.slot, c.stalker_step_x, c.stalker_step_z)
    end
    c.stalker_step_z = 0
    c.stalker_step_x = 0
    c.move_speed = speed
    if c.fall_speed ~= 0 then
        set_flags(c, F_AIRBORNE)
    else
        set_flags(c, 0, F_AIRBORNE)
    end
end

-- Clip bounds that depend on where the blend started, then the pose, the lunge's hit window and the
-- head turned towards Harry while chasing.
local function anim_update(c)
    sh.held_item_attach(vanish and 0 or c.chara_id, 2)
    local status = c.anim_status
    local to = c.stalker_blend_to
    if status == S(7, false) then
        E.clip_end_set(c.slot, S(7, false), to + 50)
    elseif status == S(7, true) then
        E.clip_start_set(c.slot, S(7, true), to + 50)
        if keyframe_in(c, 50, 58) then
            E.clip_duration_set(c.slot, status, 98304)
        elseif keyframe_in(c, 59, 65) then
            E.clip_duration_set(c.slot, status, 131072)
        else
            E.clip_duration_set(c.slot, status, q12_mul(math.max(c.move_speed, 409), 125624))
        end
    elseif status == S(37, true) then
        E.clip_duration_set(c.slot, status, q12_mul(math.max(c.move_speed, 6144), 125624))
    elseif status == S(30, true) then
        E.clip_duration_set(c.slot, status, q12_mul(math.max(c.move_speed, 409), 125624))
    elseif status == S(1, false) or status == S(42, false) then
        E.clip_end_set(c.slot, status, to)
    elseif status == S(1, true) or status == S(42, true) then
        E.clip_start_set(c.slot, status, to)
    elseif status == S(3, false) or status == S(43, false) then
        E.clip_end_set(c.slot, status, to + 9)
    elseif status == S(3, true) or status == S(43, true) then
        E.clip_start_set(c.slot, status, to + 9)
    elseif status == S(4, false) then
        E.clip_end_set(c.slot, status, to + 20)
    elseif status == S(4, true) then
        E.clip_start_set(c.slot, status, to + 20)
    elseif status == S(5, false) then
        E.clip_end_set(c.slot, status, to + 25)
    elseif status == S(5, true) then
        E.clip_start_set(c.slot, status, to + 25)
    elseif status == S(24, false) then
        E.clip_end_set(c.slot, status, to + 395)
    elseif status == S(24, true) then
        E.clip_start_set(c.slot, status, to + 395)
    end

    E.root_transform(c.slot)
    if E.anim_step(c.slot) == 0 then
        return
    end

    if keyframe_in(c, 60, 65) then
        local wx, wy, wz = E.bone_point(c.slot, 6, -5, 33, 37)
        wx, wy, wz = s32(wx << 4), s32(wy << 4), s32(wz << 4)
        local y = c.pos_y + c.box_extent_x
        local dx, dz = c.pos_x - wx, c.pos_z - wz
        local pitch = sh.math.atan2(sh.math.sqrt(s32(dx * dx + dz * dz)), wy - (c.pos_y + c.box_extent_x))
        local yaw = sh.math.atan2(wx - c.pos_x, wz - c.pos_z)
        E.attack(c.slot, 1, WEAPON_LUNGE, c.pos_x, y, c.pos_z, 0, yaw, pitch)
    end

    local head = c.stalker_head_angle
    local step = q12_mul(dt(), 512)
    local function relax()
        if head > 0 then
            head = s16(math.max(head - step, 0))
        else
            head = s16(math.min(head + step, 0))
        end
    end
    if c.control_state == 2 or c.control_state == 3 then
        local delta = angle_to_player(c)
        local p = record(0)
        local dist = distance_to(c, p.pos_x, p.pos_z)
        if dist < 12288 and abs(delta) < 512 then
            if track("predict_x") == c.stalker_target_x and track("predict_z") == c.stalker_target_z then
                if delta > 0 then
                    head = s16(math.min(delta, head + step))
                else
                    head = s16(math.max(delta, head - step))
                end
            end
        else
            relax()
        end
    else
        relax()
    end
    E.bone_rotate(c.slot, 3, 0, head, 0)
    c.stalker_head_angle = head
end

-- The collision shapes of the current pose.
local function collision_update(c)
    local status = c.anim_status
    local k = keyframe(c)
    local from = c.stalker_blend_from
    local to = c.stalker_blend_to
    local blend = function(a, b)
        collision_between(c, a, b)
    end
    local copy = function(a)
        collision_from_keyframe(c, a)
    end

    if status == S(8, false) or status == S(9, false) then
        blend(kf("GRAB", 15), kf("HOLD", 0))
    elseif status == S(8, true) or status == S(9, true) then
        copy(kf("HOLD", 0))
    elseif status == S(38, false) then
        if from == NO_VALUE then
            blend(kf("LIE_A", 0), kf("DOWN_A", 0))
        else
            blend(kf("LUNGE", 20), kf("DOWN_A", 0))
        end
    elseif status == S(21, false) then
        blend(kf("LIE_A", 0), kf("DOWN_A", 0))
    elseif status == S(21, true) or status == S(38, true) then
        blend(kf("DOWN_A", k - 353), kf("DOWN_A", k - 352))
    elseif status == S(31, false) or status == S(31, true) then
        copy(kf("DOWN_A", 8))
    elseif status == S(19, false) then
        blend(kf("LIE_B", 0), kf("DOWN_B", 0))
    elseif status == S(39, false) then
        if from == NO_VALUE then
            blend(kf("LIE_B", 0), kf("DOWN_B", 0))
        else
            blend(kf("RISE_B", from), kf("DOWN_B", 0))
        end
    elseif status == S(19, true) or status == S(39, true) then
        blend(kf("DOWN_B", k - 339), kf("DOWN_B", k - 338))
    elseif status == S(32, false) or status == S(32, true) then
        copy(kf("DOWN_B", 0))
    elseif status == S(20, false) then
        blend(kf("LIE_C", 0), kf("DOWN_C", 0))
    elseif status == S(40, false) then
        if from == NO_VALUE then
            blend(kf("LIE_C", 0), kf("DOWN_C", 0))
        else
            blend(kf("RISE_C", from), kf("DOWN_C", 0))
        end
    elseif status == S(20, true) or status == S(40, true) then
        blend(kf("DOWN_C", k - 346), kf("DOWN_C", k - 345))
    elseif status == S(33, false) or status == S(33, true) then
        copy(kf("DOWN_C", 6))
    elseif status == S(22, false) then
        copy(kf("HIT_A", 0))
    elseif status == S(22, true) then
        blend(kf("HIT_A", k - 362), kf("HIT_A", k - 361))
    elseif status == S(23, false) or status == S(41, false) then
        copy(kf("HIT_B", 0))
    elseif status == S(23, true) or status == S(41, true) then
        blend(kf("HIT_B", k - 378), kf("HIT_B", k - 377))
    elseif status == S(36, false) then
        blend(kf("HOLD", 0), kf("THROWN", 6))
    elseif status == S(10, false) then
        blend(kf("HOLD", 0), kf("THROWN", 0))
    elseif status == S(10, true) or status == S(36, true) then
        blend(kf("THROWN", k - 96), kf("THROWN", k - 95))
    elseif status == S(24, false) then
        if to ~= 0 then
            blend(kf("HIT_A", from), kf("RECOIL", to))
        else
            copy(kf("RECOIL", 0))
        end
    elseif status == S(24, true) then
        blend(kf("RECOIL", k - 394), kf("RECOIL", k - 393))
    elseif status == S(25, false) then
        if to == 83 then
            blend(kf("STAGGER", 0), kf("RECOVER", 1))
        elseif to == 23 then
            blend(kf("LUNGE", from + 20), kf("RECOVER", 1))
        elseif to == 25 then
            blend(kf("RISE_B", from), kf("RECOVER", 1))
        elseif to == 27 then
            blend(kf("RISE_C", from), kf("RECOVER", 1))
        end
    elseif status == S(25, true) then
        blend(kf("RECOVER", k - 407), kf("RECOVER", k - 406))
    elseif status == S(26, false) or status == S(26, true) then
        copy(kf("LIE_A", 0))
    elseif status == S(17, false) or status == S(17, true) then
        copy(kf("LIE_B", 0))
    elseif status == S(18, false) or status == S(18, true) then
        copy(kf("LIE_C", 0))
    elseif status == S(27, false) then
        copy(kf("GET_UP", 0))
    elseif status == S(27, true) then
        local k0 = k - 427
        local k1 = k0 ~= 15 and k - 426 or 0
        blend(kf("GET_UP", k0), kf("GET_UP", k1))
    elseif status == S(34, false) or status == S(35, false) then
        blend(kf("WALK", from), kf("GRAB", 0))
    elseif status == S(34, true) or status == S(35, true) then
        blend(kf("GRAB", k - 461), kf("GRAB", k - 460))
        c.box_extent_x = c.box_offset_y
    elseif status == S(11, false) then
        blend(kf("LIE_A", 0), kf("LUNGE", 20))
    elseif status == S(11, true) then
        blend(kf("LUNGE", k - 107 + 20), kf("LUNGE", k - 106 + 20))
    elseif status == S(12, false) then
        blend(kf("LIE_B", 0), kf("RISE_B", 0))
    elseif status == S(12, true) then
        blend(kf("RISE_B", k - 129), kf("RISE_B", k - 128))
    elseif status == S(13, false) then
        blend(kf("LIE_C", 0), kf("RISE_C", 0))
    elseif status == S(13, true) then
        blend(kf("RISE_C", k - 158), kf("RISE_C", k - 157))
    elseif status == S(7, false) then
        blend(kf("WALK", from), kf("LUNGE", to))
    elseif status == S(7, true) then
        blend(kf("LUNGE", k - 50), kf("LUNGE", k - 49))
        c.box_extent_x = -2867
        c.stalker_blend_from = 15
    elseif status == S(1, false) or status == S(42, false) then
        blend(kf("LUNGE", 19 + from), kf("TURN_1", to))
    elseif status == S(1, true) or status == S(42, true) then
        blend(kf("TURN_1", k), kf("TURN_1", k + 1))
    elseif status == S(3, false) or status == S(43, false) then
        blend(kf("LUNGE", 19 + from), kf("TURN_3", to))
    elseif status == S(3, true) or status == S(43, true) then
        blend(kf("TURN_3", k - 10 + 1), kf("TURN_3", k - 9 + 1))
    elseif status == S(2, false) or status == S(44, false) then
        blend(kf("LUNGE", from - 1 + 20), kf("TURN_2", 0))
    elseif status == S(2, true) or status == S(44, true) then
        blend(kf("TURN_2", k - 4), kf("TURN_2", k - 3))
    elseif status == S(4, false) then
        blend(kf("RISE_B", from), kf("TURN_4", to))
    elseif status == S(4, true) then
        blend(kf("TURN_4", k - 20), kf("TURN_4", k - 19))
    elseif status == S(5, false) then
        if to >= 2 then
            blend(kf("RISE_B", from), kf("TURN_5_FROM", to))
        else
            copy(kf("TURN_5", 0))
        end
    elseif status == S(5, true) then
        blend(kf("TURN_5", k - 25), kf("TURN_5", k - 25 + 1))
    elseif status == S(45, true) then
        blend(kf("RISE_C", k - 158 + 1), kf("RISE_C", k - 158))
    elseif status == S(30, false) then
        if from == 15 then
            blend(kf("LUNGE", 19), kf("WALK", 0))
        elseif from == 55 then
            blend(kf("GET_UP", to), kf("WALK", 0))
        end
    elseif status == S(30, true) or status == S(37, true) then
        local k0 = k - 477
        local k1 = k0 ~= 29 and k - 476 or 0
        blend(kf("WALK", k0), kf("WALK", k1))
    end
    E.shape_offsets_update(c.slot)
end

local function difficulty_update()
    local d = sh.save.game_difficulty()
    if d == NORMAL then
        max_health, down_health, regen, stagger_damage = 1433600, 409600, 12288, 204800
    elseif d == EASY then
        max_health, down_health, regen, stagger_damage = 1433600, 409600, 0, 143360
    else
        max_health, down_health, regen, stagger_damage = 2457600, 737280, 24576, 409600
    end
end

function update(slot)
    local c = record(slot)
    difficulty_update()
    if c.control_state == 0 then
        init(c)
    end
    if c.control_state ~= 1 then
        if dt() ~= 0 then
            damage_update(c)
            control_update(c)
            move_update(c)
        end
        anim_update(c)
        collision_update(c)
        if dt() ~= 0 then
            sfx_update(c)
        end
        set_flags(c, 0, F_WARP_TURN)
    end
    if vanish then
        vanish = false
        c.chara_id = 0
    end
end
