-- The Options screen (title OPTION and the pause menu's OPTION open the same one). The options
-- game state draws this menu in the original options look (gamedata/menu/options.json, "tree").
-- Natives: setting.<config key>, the grouped rows, game.<name> (the original options' rows) and options.*
-- in src/ui/menu/options_binds.cpp.
-- Texts: gamedata/text/<lang>/options_menu.json. Every setting appears on exactly one page.
-- A mod adds a page with sh.menu.add_page{ parent = "options", ... }: it is linked
-- from Mod Options (mods.lua), a root row that shows once a mod has a page;
-- rows whose id starts with "tail." stay last.

local function setting(key, native)
    return {
        kind = "setting",
        id = key,
        label = "ui.options." .. key,
        help = "ui.options." .. key .. ".help",
        native = native or ("setting." .. key),
    }
end

-- A row of the original options screen (vibration, volumes, the extra options).
local function game(key)
    return setting(key, "game." .. key)
end

local function action(key, native)
    return {
        kind = "action",
        id = key,
        label = "ui.options." .. key,
        help = "ui.options." .. key .. ".help",
        native = native,
    }
end

local function page(key)
    return {
        kind = "submenu",
        id = key,
        label = "ui.options." .. key,
        help = "ui.options." .. key .. ".help",
        submenu = "options." .. key,
    }
end

-- A row shown only while every condition holds (visible_if).
local function only(row, ...)
    row.visible_if = { ... }
    return row
end

sh.menu.define{
    id = "options.display",
    title = "ui.options.display",
    rows = {
        setting("resolution_aspect"),
        setting("resolution"),
        setting("fullscreen"),
        setting("vsync"),
        setting("fps_cap"),
        setting("widescreen_mode"),
        action("brightness", "options.brightness"),
    },
}

-- A caption over a group of rows; it hides itself while no row of its group is shown.
local function header(key)
    return { kind = "header", id = "group." .. key, label = "ui.options.group." .. key }
end

-- Only the native Vulkan renderer runs these rows; they come and go with the Renderer row at once, before the
-- restart that brings the renderer (a row the running renderer or the GPU cannot do is locked and says why).
local on_vulkan = { key = "renderer", is = "vulkan" }

local function vulkan(row, ...)
    row.visible_if = { on_vulkan, ... }
    return row
end

-- Only the per-pixel beam casts shadows: with the classic per-vertex one (flashlight_mode 0) the shadow rows
-- would change nothing, so they are not shown.
local per_pixel_beam = { key = "flashlight_mode", none = { 0 } }

-- Renderer first, the preset second (it stands for most rows below: settings_presets.cpp, RENDER.md 7.2), then
-- every feature on its own row in groups, so a player can mix them; the preset reads Custom once they differ.
local renderer_pending_at_open = false

sh.menu.define{
    id = "options.graphics",
    title = "ui.options.graphics",
    rows = {
        -- The renderer is chosen at start: a change waits for a restart, offered by the row below it.
        setting("renderer"),
        {
            kind = "action",
            id = "restart",
            label = "ui.options.restart",
            help = "ui.options.restart.help",
            native = "options.restart",
            confirm = "ui.options.restart.confirm",
        },
        setting("graphics_preset"),

        header("display"),
        vulkan(setting("present_mode")),
        vulkan(setting("frames_in_flight")),
        vulkan(setting("present_wait")),
        -- HDR10 output shows while the HDR frame (Effects) is in effect, its levels while the output is.
        vulkan(setting("hdr_output")),
        vulkan(setting("hdr_paper_white")),
        vulkan(setting("hdr_peak")),
        setting("load_screen_trail"),

        header("image_quality"),
        setting("render_scale"),
        vulkan(setting("upscaler")),
        vulkan(setting("upscale_quality")),
        vulkan(setting("upscale_sharpness")),
        setting("texture_filter"),
        -- The native Vulkan backend has no multisampled targets; TAA is its anti-aliasing.
        only(setting("msaa"), { key = "renderer", none = { "vulkan" } }),
        setting("use_pgxp"),

        header("lighting"),
        {
            kind = "submenu",
            id = "flashlight_lighting",
            label = "ui.options.flashlight_lighting",
            help = "ui.options.flashlight_lighting.help",
            submenu = "options.lighting",
        },
        only(setting("shadow_quality"), per_pixel_beam),
        only(setting("shadow_filter"), per_pixel_beam),
        vulkan(setting("map_lights")),
        vulkan(setting("sun_shadows")),
        vulkan(setting("particle_shadows")),
        setting("extra_lights"),

        -- KHR ray tracing; the effects show while the master row is on.
        header("ray_tracing"),
        vulkan(setting("ray_tracing")),
        vulkan(setting("rt_shadows"), { key = "ray_tracing" }),
        vulkan(setting("rt_reflections"), { key = "ray_tracing" }),
        vulkan(setting("rt_gi"), { key = "ray_tracing" }),
        vulkan(setting("rt_lights"), { key = "ray_tracing" }),

        header("models_textures"),
        vulkan(setting("model_upscaling")),
        setting("texture_upscale"),

        header("effects"),
        -- The rows after the HDR frame show while it is in effect.
        vulkan(setting("hdr_frame")),
        vulkan(setting("hdr_tonemap")),
        vulkan(setting("bloom")),
        vulkan(setting("ambient_occlusion")),
        vulkan(setting("ao_strength")),
        vulkan(setting("volumetric_fog")),
        vulkan(setting("fog_scatter")),
        vulkan(setting("god_rays")),
        setting("fog_strength"),
        setting("post_process"),
        setting("bullet_decals"),
        setting("persistent_remains"),
    },
    -- Leaving after a change of the renderer offers the restart, as the Mods screen does; a choice that was
    -- already pending when the page opened is not asked about again.
    on_open = function()
        renderer_pending_at_open = sh.menu.native_visible("options.restart")
    end,
    leave_confirm = function()
        if not renderer_pending_at_open and sh.menu.native_visible("options.restart") then
            return "ui.options.restart.confirm"
        end
    end,
    on_leave = function(yes)
        if yes then
            sh.menu.run("options.restart")
        end
    end,
}

-- Classic is the original look, Modern the soft one the default preset ships. The beam's shadow rows are on the
-- Graphics page (Lighting and shadows).
sh.menu.define{
    id = "options.lighting",
    title = "ui.options.lighting",
    rows = {
        setting("flashlight_style"),
        setting("flashlight_intensity"),
        setting("flashlight_size"),
        setting("lighting_style"),
    },
}

sh.menu.define{
    id = "options.audio",
    title = "ui.options.audio",
    rows = {
        setting("music_volume"),
        setting("effects_volume"),
        game("voice_volume"),
        setting("fmv_volume"),
        setting("audio_output"),
        setting("cutscene_line_gap_ms"),
    },
}

sh.menu.define{
    id = "options.controls",
    title = "ui.options.controls",
    rows = {
        action("keybinds", "options.edit_keybinds"),
        action("controls_reference", "options.controls_reference"),
        game("vibration"),
        setting("control_2d"),
        setting("mouse_sensitivity"),
        setting("controller_sensitivity"),
        setting("invert_mouse_y"),
        setting("invert_controller_y"),
        setting("aim_assist"),
        setting("disable_dpad_movement"),
        setting("quick_weapon_switch"),
    },
}

-- The original's extra options are the last rows; View Mode and Bullet Adjust show once unlocked.
sh.menu.define{
    id = "options.gameplay",
    title = "ui.options.gameplay",
    rows = {
        game("auto_load"),
        setting("auto_reload"),
        setting("aim_resume_after_inventory"),
        setting("quick_save_instant"),
        setting("quick_save_keys"),
        game("weapon_control"),
        game("blood_color"),
        game("view_control"),
        game("retreat_turn"),
        game("walk_run_control"),
        game("auto_aiming"),
        game("view_mode"),
        game("bullet_adjust"),
    },
}

local function camera_is(...)
    return { key = "control_style", any = { ... } }
end

local crosshair_on = { key = "crosshair" }

-- The camera kind (F9 switches it in game) and the rows of the PC's alternative cameras: each row
-- shows only for the cameras it applies to, so the classic fixed camera has the first row alone.
sh.menu.define{
    id = "options.camera",
    title = "ui.options.camera",
    rows = {
        setting("control_style"),
        only(setting("tps_fov"), camera_is("tps")),
        only(setting("tps_ots_aim"), camera_is("tps")),
        only(setting("ots_fov"), camera_is("ots")),
        only(setting("tps_camera_smoothing"), camera_is("tps", "ots")),
        only(setting("tps_look_smoothing"), camera_is("tps", "ots")),
        only(setting("tps_camera_recenter"), camera_is("tps", "ots")),
        only(setting("tps_camera_collision"), camera_is("tps", "ots")),
        only(setting("fps_fov"), camera_is("fps")),
        only(setting("crosshair"), camera_is("tps", "ots", "fps")),
        only(setting("crosshair_style"), camera_is("tps", "ots", "fps"), crosshair_on),
        only(setting("crosshair_size"), camera_is("tps", "ots", "fps"), crosshair_on),
        only(setting("crosshair_fps_aim_only"), camera_is("tps", "ots", "fps"), crosshair_on),
        only(setting("crosshair_hide_melee"), camera_is("tps", "ots", "fps"), crosshair_on),
    },
}

sh.menu.define{
    id = "options.interface",
    title = "ui.options.interface",
    rows = {
        setting("language"),
        setting("skip_intros"),
        setting("low_health_glow"),
        setting("text_size"),
        setting("debug_options"),
        setting("modding_tools"),
    },
}

sh.menu.define{
    id = "options",
    title = "ui.options.title",
    rows = {
        action("exit", "options.exit"),
        page("display"),
        page("graphics"),
        page("audio"),
        page("controls"),
        page("camera"),
        page("gameplay"),
        page("interface"),
        -- The quick setup of the first start: the game restarts into it.
        {
            kind = "action",
            id = "quick_setup",
            label = "ui.options.quick_setup",
            help = "ui.options.quick_setup.help",
            native = "options.quick_setup",
            confirm = "ui.options.quick_setup.confirm",
        },
        {
            kind = "action",
            id = "tail.reset_defaults",
            label = "ui.options.reset_defaults",
            help = "ui.options.reset_defaults.help",
            native = "options.reset_defaults",
            confirm = "ui.options.reset_defaults.confirm",
        },
    },
}
