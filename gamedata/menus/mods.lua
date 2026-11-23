-- The Mods screen: Options > Mods (menu "options.mods"), one submenu "options.mods.<id>" per installed mod, built
-- from sh.mods.list(). Every edit is saved to mods/load_order.json at once and loads on the next start (mods with
-- "requires_restart": false at once); the menus are defined again after each edit so they show the pending choice.
-- Mods change only in Options opened from the title (sh.mods.editable()); leaving the page after a change that
-- needs a restart offers one. Texts: gamedata/text/<lang>/mods_menu.json.

local WRAP = 30  -- characters per read-only line in the 248-pixel row column

local function utf8_len(s)
    return select(2, s:gsub("[^\128-\191]", ""))
end

-- The first `n` characters of `s` and the rest (never splits a UTF-8 sequence).
local function utf8_split(s, n)
    local count, cut = 0, #s + 1
    for pos in s:gmatch("()[^\128-\191]") do
        if count == n then
            cut = pos
            break
        end
        count = count + 1
    end
    return s:sub(1, cut - 1), s:sub(cut)
end

-- Word-wraps `text` (newlines kept) into lines of at most `width` characters; a longer word (a path) is cut.
local function wrap(text, width)
    local out = {}
    for para in (tostring(text or "") .. "\n"):gmatch("(.-)\r?\n") do
        local line = ""
        for word in para:gmatch("%S+") do
            while utf8_len(word) > width do
                if line ~= "" then
                    out[#out + 1] = line
                    line = ""
                end
                local head, rest = utf8_split(word, width)
                out[#out + 1] = head
                word = rest
            end
            if line == "" then
                line = word
            elseif utf8_len(line) + 1 + utf8_len(word) <= width then
                line = line .. " " .. word
            else
                out[#out + 1] = line
                line = word
            end
        end
        if line ~= "" then
            out[#out + 1] = line
        end
    end
    return out
end

local function info(label, text)
    return { kind = "info", label = label, text = text }
end

local function add_lines(rows, label, lines, prefix)
    if #lines == 0 then
        return
    end
    rows[#rows + 1] = info(label)
    for _, entry in ipairs(lines) do
        for _, l in ipairs(wrap((prefix or "") .. entry, WRAP)) do
            rows[#rows + 1] = info(l)
        end
    end
end

local function restart_rows(rows)
    rows[#rows + 1] = info("ui.mods.restart")
    rows[#rows + 1] = {
        kind = "info",
        label = "ui.mods.restart_pending",
        visible = function() return sh.mods.restart_pending() end,
    }
end

local rebuild
local built_editable  -- sh.mods.editable() when the menus were last built
local changed = false  -- an edit this visit still waits for a restart

local function editable()
    return sh.mods.editable()
end

-- Saves the pending choice, applies what needs no restart and shows it; a failed save is logged.
local function commit()
    local ok, err = sh.mods.save()
    if not ok then
        sh.log("mods: " .. tostring(err))
    else
        sh.mods.apply_live()
    end
    changed = sh.mods.restart_pending()
    rebuild()
end

local function locked_note(rows)
    rows[#rows + 1] = { kind = "info", label = "ui.mods.locked.row", visible = function() return not editable() end }
end

local function find(id)
    for _, m in ipairs(sh.mods.list()) do
        if m.id == id then
            return m
        end
    end
end

local function mod_menu(m)
    local id = m.id
    local rows = {
        {
            kind = "toggle", id = "enabled", label = "ui.mods.enabled",
            help = built_editable and "ui.mods.enabled.help" or "ui.mods.locked", enabled = editable,
            get = function()
                local cur = find(id)
                return cur ~= nil and cur.enabled
            end,
            set = function(on)
                sh.mods.set_enabled(id, on)
                commit()
            end,
        },
        {
            kind = "action", id = "up", label = "ui.mods.move_up",
            help = built_editable and "ui.mods.move_up.help" or "ui.mods.locked", enabled = editable,
            activate = function()
                if sh.mods.move(id, -1) then commit() end
            end,
        },
        {
            kind = "action", id = "down", label = "ui.mods.move_down",
            help = built_editable and "ui.mods.move_down.help" or "ui.mods.locked", enabled = editable,
            activate = function()
                if sh.mods.move(id, 1) then commit() end
            end,
        },
    }
    locked_note(rows)
    restart_rows(rows)
    rows[#rows + 1] = info("ui.mods.state." .. m.state)
    rows[#rows + 1] = info("ui.mods.version", m.version)
    if m.load_index > 0 then
        rows[#rows + 1] = info("ui.mods.loaded", "#" .. m.load_index)
    else
        rows[#rows + 1] = info("ui.mods.not_loaded")
    end
    add_lines(rows, "ui.mods.problems", m.problems, "- ")
    if m.gameplay then
        rows[#rows + 1] = info("ui.mods.gameplay")
    end
    if m.scripts then
        rows[#rows + 1] = info("ui.mods.scripts")
    end
    if m.load_index > 0 then
        rows[#rows + 1] = info("ui.mods.files_replaced", tostring(m.files_replaced))
        rows[#rows + 1] = info("ui.mods.files_added", tostring(m.files_added))
        if m.files_overridden > 0 then
            rows[#rows + 1] = info("ui.mods.files_overridden", tostring(m.files_overridden))
        end
    end
    add_lines(rows, "ui.mods.authors", #m.authors > 0 and { table.concat(m.authors, ", ") } or {})
    add_lines(rows, "ui.mods.description", m.description ~= "" and { m.description } or {})
    add_lines(rows, "ui.mods.homepage", m.homepage ~= "" and { m.homepage } or {})
    add_lines(rows, "ui.mods.warnings", m.warnings, "- ")
    sh.menu.define{ id = "options.mods." .. id, title = m.name, rows = rows }
end

rebuild = function()
    built_editable = editable()
    local mods = sh.mods.list()
    local rows = {}
    locked_note(rows)
    if #mods == 0 then
        rows[#rows + 1] = info("ui.mods.none")
        local dir = sh.mods.dir()
        for _, l in ipairs(wrap(dir ~= "" and dir or "<exe>/mods", WRAP)) do
            rows[#rows + 1] = info(l)
        end
    end
    for _, m in ipairs(mods) do
        mod_menu(m)
        rows[#rows + 1] = {
            kind = "submenu",
            id = m.id,
            -- Labels are literals here; the localized state is the row's help line.
            label = (m.state == "active" and "" or "- ") .. m.name,
            help = built_editable and "ui.mods.row." .. m.state or "ui.mods.locked",
            submenu = "options.mods." .. m.id,
        }
    end
    restart_rows(rows)
    -- The version a mod's "min_engine_version" is compared with; a name and a number, the same in every language.
    rows[#rows + 1] = info("Silent Hill PC " .. sh.engine.version)
    -- add_page keeps the link in "options" whatever order the files run in (options.lua sorts after this file) and
    -- when a mod defines "options" again; links other mods add to "options.mods.<id>" survive these rebuilds too.
    sh.menu.add_page{
        parent = "options", id = "options.mods", link_id = "mods", title = "ui.mods.title",
        label = "ui.mods.title", help = "ui.mods.help", rows = rows,
        -- The same Options tree serves the title and the pause menu: the rows follow where it was opened from.
        on_open = function()
            if built_editable ~= editable() then
                rebuild()
            end
        end,
        leave_confirm = function()
            if changed and editable() and sh.mods.restart_pending() then
                return "ui.mods.restart_now"
            end
        end,
        on_leave = function(yes)
            changed = false
            if yes then
                local ok, err = sh.mods.restart()
                if not ok then
                    sh.log("mods: cannot restart: " .. tostring(err))
                end
            end
        end,
    }
end

rebuild()

-- Mod Options: the pages mods add with sh.menu.add_page{ parent = "options", ... } are linked here (the engine
-- moves them off the Options root), one row per mod page in load order. Unlike the Mods screen it works in play.
-- Defined once every file ran, and only when a mod added a page: the Options root has no row to spare for an
-- empty one.
sh.menu.after_load(function()
    if sh.menu.page_count("options.mod_options") == 0 then
        return
    end
    sh.menu.add_page{
        parent = "options", id = "options.mod_options", link_id = "mod_options", title = "ui.mod_options.title",
        label = "ui.mod_options.title", help = "ui.mod_options.help", rows = {},
    }
end)
