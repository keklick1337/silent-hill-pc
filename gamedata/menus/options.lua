-- The Options screen (title OPTION and the pause menu's OPTION open the same one). The options
-- game state draws this menu in the original options look (gamedata/menu/options.json, "tree").
-- Natives: setting.<config key>, the grouped rows, game.<name> (the original options' rows) and options.*
-- in src/ui/menu/options_binds.cpp.
-- Texts: gamedata/text/<lang>/options_menu.json. Every setting appears on exactly one page (a phone or tablet shows
-- a few on another page: the rows marked `mobile = true` stand in for those marked `mobile = false`).
-- A phone or tablet shows the settings most players change; every other row is marked `advanced` and shows there on
-- the page's Advanced page, which the engine links at the page's end (docs/modding/scripting.md).
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

-- Only on a desktop, only on a phone or tablet, and on a phone or tablet on the page's Advanced page.
local function desktop(row)
    row.mobile = false
    return row
end

local function phone(row)
    row.mobile = true
    return row
end

local function advanced(row)
    row.advanced = true
    return row
end

-- A phone or tablet draws at the size of its screen: it has no window, resolution or aspect to choose (the render
-- scale on the Graphics page is its size control), so it has no Display page; its other rows are on Graphics there.
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
        advanced(setting("renderer")),
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
        -- A phone presents with the display's own rhythm (FIFO at the rate fps_cap asks of the display).
        desktop(vulkan(setting("present_mode"))),
        advanced(vulkan(setting("frames_in_flight"))),
        advanced(vulkan(setting("present_wait"))),
        -- HDR10 output shows while the HDR frame (Effects) is in effect, its levels while the output is.
        advanced(vulkan(setting("hdr_output"))),
        advanced(vulkan(setting("hdr_paper_white"))),
        advanced(vulkan(setting("hdr_peak"))),
        -- The Display page's rows a phone or tablet has (it has no Display page).
        advanced(phone(setting("widescreen_mode"))),
        advanced(phone(action("brightness", "options.brightness"))),
        advanced(setting("load_screen_trail")),

        header("image_quality"),
        setting("render_scale"),
        phone(setting("fps_cap")),
        advanced(vulkan(setting("upscaler"))),
        advanced(vulkan(setting("upscale_quality"))),
        advanced(vulkan(setting("upscale_sharpness"))),
        advanced(setting("texture_filter")),
        -- The native Vulkan backend has no multisampled targets; TAA is its anti-aliasing.
        advanced(only(setting("msaa"), { key = "renderer", none = { "vulkan" } })),
        advanced(setting("use_pgxp")),

        header("lighting"),
        phone(setting("flashlight_style")),
        advanced({
            kind = "submenu",
            id = "flashlight_lighting",
            label = "ui.options.flashlight_lighting",
            help = "ui.options.flashlight_lighting.help",
            submenu = "options.lighting",
        }),
        only(setting("shadow_quality"), per_pixel_beam),
        advanced(only(setting("shadow_filter"), per_pixel_beam)),
        vulkan(setting("map_lights")),
        vulkan(setting("sun_shadows")),
        advanced(vulkan(setting("particle_shadows"))),
        setting("extra_lights"),

        -- KHR ray tracing; the effects show while the master row is on. A phone or tablet has none: the trace alone
        -- took 38 ms a frame on a 2024 flagship (render_preset.cpp switches the keys off there).
        header("ray_tracing"),
        desktop(vulkan(setting("ray_tracing"))),
        desktop(vulkan(setting("rt_shadows"), { key = "ray_tracing" })),
        desktop(vulkan(setting("rt_reflections"), { key = "ray_tracing" })),
        desktop(vulkan(setting("rt_gi"), { key = "ray_tracing" })),
        desktop(vulkan(setting("rt_lights"), { key = "ray_tracing" })),

        -- The AI upscalers are a desktop's: a phone or tablet has neither (src/engine/app/texture_upscale.cpp).
        header("models_textures"),
        desktop(vulkan(setting("model_upscaling"))),
        desktop(setting("texture_upscale")),

        header("effects"),
        -- The rows after the HDR frame show while it is in effect.
        advanced(vulkan(setting("hdr_frame"))),
        advanced(vulkan(setting("hdr_tonemap"))),
        advanced(vulkan(setting("bloom"))),
        advanced(vulkan(setting("ambient_occlusion"))),
        advanced(vulkan(setting("ao_strength"))),
        advanced(vulkan(setting("volumetric_fog"))),
        advanced(vulkan(setting("fog_scatter"))),
        advanced(vulkan(setting("god_rays"))),
        setting("fog_strength"),
        advanced(setting("post_process")),
        advanced(setting("bullet_decals")),
        advanced(setting("persistent_remains")),
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
-- Graphics page (Lighting and shadows), and so is the Flashlight row on a phone or tablet.
sh.menu.define{
    id = "options.lighting",
    title = "ui.options.lighting",
    rows = {
        desktop(setting("flashlight_style")),
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
        advanced(setting("audio_output")),
        advanced(setting("cutscene_line_gap_ms")),
    },
}

-- The on-screen controls of a touch screen (src/ui/menu/options_touch.cpp): a page of their own on a desktop with a
-- touch screen, the first rows of Controls on a phone or tablet.
local function touch_rows()
    return {
        action("touch_edit_layout", "options.touch_edit_layout"),
        setting("touch_controls"),
        -- The stick or a d-pad of four arrows with a run toggle; a phone's choice (a desktop tests with the mouse).
        phone(setting("touch_movement")),
        setting("touch_action_button"),
        setting("touch_aim_toggle"),
        only(setting("touch_stick_run"), { key = "touch_movement", is = "stick" }),
        setting("touch_opacity"),
        setting("touch_scale"),
        {
            kind = "action",
            id = "touch_reset_layout",
            label = "ui.options.touch_reset_layout",
            help = "ui.options.touch_reset_layout.help",
            native = "options.touch_reset_layout",
            confirm = "ui.options.touch_reset_layout.confirm",
        },
    }
end

sh.menu.define{
    id = "options.touch",
    title = "ui.options.touch",
    rows = touch_rows(),
}

local controls_rows = {}
for _, row in ipairs(touch_rows()) do
    -- Resetting the layout is rarely wanted, so it waits on the Advanced page.
    if row.id == "touch_reset_layout" then
        advanced(row)
    end
    controls_rows[#controls_rows + 1] = phone(row)
end
for _, row in ipairs{
    desktop(only(page("touch"), { key = "touch_device" })),
    -- Keys and pad buttons: a phone or tablet plays with the touch controls above.
    desktop(action("keybinds", "options.edit_keybinds")),
    desktop(action("controls_reference", "options.controls_reference")),
    game("vibration"),
    -- A phone's own vibrator plays the rumble while no pad with motors is in use (src/input/phone_rumble.h).
    phone(setting("phone_vibration")),
    -- A drag over the free screen turns the alternate cameras (src/input/touch/touch_controls.h).
    phone(setting("touch_look_sensitivity")),
    advanced(setting("control_2d")),
    -- Every look input of the free cameras at once, on top of each device's own factor.
    setting("look_speed"),
    desktop(setting("mouse_sensitivity")),
    advanced(setting("controller_sensitivity")),
    -- How far a stick moves before it counts, and the rim that counts as a full push (src/input/stick_shape.h).
    setting("pad_deadzone"),
    advanced(setting("pad_outer_deadzone")),
    advanced(setting("pad_look_deadzone")),
    desktop(setting("invert_mouse_y")),
    advanced(setting("invert_controller_y")),
    advanced(setting("aim_assist")),
    advanced(setting("disable_dpad_movement")),
    advanced(setting("quick_weapon_switch")),
} do
    controls_rows[#controls_rows + 1] = row
end

sh.menu.define{
    id = "options.controls",
    title = "ui.options.controls",
    rows = controls_rows,
}

-- The original's extra options are the last rows; View Mode and Bullet Adjust show once unlocked.
sh.menu.define{
    id = "options.gameplay",
    title = "ui.options.gameplay",
    rows = {
        game("auto_load"),
        advanced(setting("auto_reload")),
        advanced(setting("aim_resume_after_inventory")),
        desktop(setting("quick_save_instant")),
        desktop(setting("quick_save_keys")),
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
        only(setting("tps_camera_smoothing"), camera_is("tps")),
        only(setting("tps_look_smoothing"), camera_is("tps")),
        only(setting("tps_camera_recenter"), camera_is("tps")),
        only(setting("tps_camera_collision"), camera_is("tps")),
        only(setting("fps_fov"), camera_is("fps")),
        only(setting("crosshair"), camera_is("tps", "fps")),
        only(setting("crosshair_style"), camera_is("tps", "fps"), crosshair_on),
        only(setting("crosshair_size"), camera_is("tps", "fps"), crosshair_on),
        only(setting("crosshair_fps_aim_only"), camera_is("tps", "fps"), crosshair_on),
        only(setting("crosshair_hide_melee"), camera_is("tps", "fps"), crosshair_on),
    },
}

sh.menu.define{
    id = "options.interface",
    title = "ui.options.interface",
    rows = {
        setting("language"),
        setting("skip_intros"),
        advanced(setting("low_health_glow")),
        setting("text_size"),
        setting("fps_overlay"),
        setting("debug_options"),
        advanced(setting("modding_tools")),
    },
}

sh.menu.define{
    id = "options",
    title = "ui.options.title",
    rows = {
        action("exit", "options.exit"),
        desktop(page("display")),
        page("graphics"),
        page("audio"),
        page("controls"),
        -- A phone lists it too: the camera style is the first thing a touch player may want to change.
        page("camera"),
        page("gameplay"),
        page("interface"),
        -- The quick setup of the first start: the game restarts into it (a desktop's questions; a phone has none).
        desktop({
            kind = "action",
            id = "quick_setup",
            label = "ui.options.quick_setup",
            help = "ui.options.quick_setup.help",
            native = "options.quick_setup",
            confirm = "ui.options.quick_setup.confirm",
        }),
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
