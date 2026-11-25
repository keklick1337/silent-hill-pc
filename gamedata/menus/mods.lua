-- The Mods screen: Options > Mods (menu "options.mods"), one submenu "options.mods.<id>" per installed mod, built
-- from sh.mods.list(). Every edit is saved to mods/load_order.json at once and loads on the next start (mods with
-- "requires_restart": false at once); the menus are defined again after each edit so they show the pending choice.
-- Mods change only in Options opened from the title (sh.mods.editable()); leaving the page after a change that
-- needs a restart offers one. Options > Mods > Import mods ("options.mods.import") adds mods from a folder or a .zip
-- the player picks (sh.mods.import_*). Texts: gamedata/text/<lang>/mods_menu.json.

-- Read-only lines are notes (small = true): smaller text under the rows, long ones wrapped or cut by the engine.
local function note(label, text, more)
    local row = { kind = "info", small = true, label = label, text = text }
    for k, v in pairs(more or {}) do
        row[k] = v
    end
    return row
end

local function add_lines(rows, label, lines, prefix, cut)
    if #lines == 0 then
        return
    end
    rows[#rows + 1] = note(label)
    for _, entry in ipairs(lines) do
        rows[#rows + 1] = note((prefix or "") .. entry, nil, { cut = cut or "wrap" })
    end
end

local function restart_rows(rows)
    rows[#rows + 1] = note("ui.mods.restart")
    rows[#rows + 1] = note("ui.mods.restart_pending", nil, {
        visible = function() return sh.mods.restart_pending() end,
    })
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
    rows[#rows + 1] = note("ui.mods.locked.row", nil, { visible = function() return not editable() end })
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
    rows[#rows + 1] = note("ui.mods.state." .. m.state)
    rows[#rows + 1] = note("ui.mods.version", m.version)
    if m.load_index > 0 then
        rows[#rows + 1] = note("ui.mods.loaded", "#" .. m.load_index)
    else
        rows[#rows + 1] = note("ui.mods.not_loaded")
    end
    add_lines(rows, "ui.mods.problems", m.problems, "- ")
    if m.gameplay then
        rows[#rows + 1] = note("ui.mods.gameplay")
    end
    if m.scripts then
        rows[#rows + 1] = note("ui.mods.scripts")
    end
    if m.load_index > 0 then
        rows[#rows + 1] = note("ui.mods.files_replaced", tostring(m.files_replaced))
        rows[#rows + 1] = note("ui.mods.files_added", tostring(m.files_added))
        if m.files_overridden > 0 then
            rows[#rows + 1] = note("ui.mods.files_overridden", tostring(m.files_overridden))
        end
    end
    add_lines(rows, "ui.mods.authors", #m.authors > 0 and { table.concat(m.authors, ", ") } or {})
    add_lines(rows, "ui.mods.description", m.description ~= "" and { m.description } or {})
    -- A web address has no spaces to wrap at: its start and its end show.
    add_lines(rows, "ui.mods.homepage", m.homepage ~= "" and { m.homepage } or {}, nil, "middle")
    add_lines(rows, "ui.mods.warnings", m.warnings, "- ")
    sh.menu.define{ id = "options.mods." .. id, title = m.name, rows = rows }
end

-- ---- Import mods: pick a folder or a .zip (or drop one on the window), check the list, import

local IMPORT = "options.mods.import"
local import_state = "idle"  -- sh.mods.import_poll(): "idle", "picking" (a dialog is open), "ready" (a scan to show)
local define_import

local function import_rows_found(rows)
    local found = sh.mods.import_found()
    -- The name is enough to recognise the pick; the full path (on a phone a copy in the app's storage) says no more.
    rows[#rows + 1] = note("ui.mods.import.source", found.source, { cut = "path" })
    if found.error ~= "" then
        rows[#rows + 1] = note("ui.mods.import.error." .. found.error)
    else
        rows[#rows + 1] = note("ui.mods.import.found", tostring(#found.mods))
    end
    local chosen = {}
    for _, m in ipairs(found.mods) do
        local index = m.index
        chosen[index] = m.selected
        rows[#rows + 1] = {
            -- A long name ends in "..." before the On / Off column; the version is on the note under it.
            kind = "toggle", id = "mod." .. index, label = m.name, cut = "end",
            help = "ui.mods.import.status." .. m.status,
            enabled = function() return m.status ~= "invalid" and editable() end,
            get = function() return chosen[index] == true end,
            set = function(on)
                if sh.mods.import_select(index, on) then
                    chosen[index] = on
                end
            end,
        }
        if m.status == "invalid" then
            rows[#rows + 1] = note("ui.mods.import.invalid." .. m.reason, nil, { cut = "wrap" })
            if m.detail ~= "" then
                rows[#rows + 1] = note(m.detail, nil, { cut = "wrap" })
            end
        else
            rows[#rows + 1] = note("ui.mods.import.line." .. m.status, nil, {
                cut = "wrap", args = { version = m.version, installed = m.installed_version },
            })
        end
        for _, n in ipairs(m.notes) do
            rows[#rows + 1] = note("ui.mods.import.note." .. n.kind, nil, {
                cut = "wrap", args = { need = n.need, have = n.have },
            })
        end
    end
    local function any_chosen()
        for _, on in pairs(chosen) do
            if on then
                return true
            end
        end
        return false
    end
    rows[#rows + 1] = {
        kind = "action", id = "run", label = "ui.mods.import.run", help = "ui.mods.import.run.help",
        enabled = function() return editable() and any_chosen() end,
        activate = function()
            local ok, err = sh.mods.import_run()
            if not ok then
                sh.log("mods: import failed: " .. tostring(err))
            end
            import_state = "idle"
            rebuild()
            define_import()
            -- The page now starts with what the import did: brought into view from the Import row far below.
            sh.menu.focus(IMPORT, "result")
        end,
    }
    rows[#rows + 1] = {
        kind = "action", id = "cancel", label = "ui.mods.import.cancel", help = "ui.mods.import.cancel.help",
        activate = function()
            sh.mods.import_clear()
            import_state = "idle"
            define_import()
        end,
    }
end

local function import_rows_pick(rows)
    local report, err = sh.mods.import_last()
    if report then
        -- The outcome keeps the full size of a row: it is what the player looks for after Import.
        rows[#rows + 1] = {
            kind = "info", id = "result", label = "ui.mods.import.done",
            text = tostring(#report.added + #report.replaced),
        }
        for _, id in ipairs(report.added) do
            rows[#rows + 1] = note("+ " .. id, nil, { cut = "end" })
        end
        for _, id in ipairs(report.replaced) do
            rows[#rows + 1] = note("* " .. id, nil, { cut = "end" })
        end
        rows[#rows + 1] = note("ui.mods.import.done_note", nil, { cut = "wrap" })
    elseif err ~= "" then
        rows[#rows + 1] = { kind = "info", id = "result", label = "ui.mods.import.failed" }
        rows[#rows + 1] = note(err, nil, { cut = "wrap" })
    end
    rows[#rows + 1] = {
        kind = "action", id = "folder", label = "ui.mods.import.folder", help = "ui.mods.import.folder.help",
        visible = function() return sh.mods.import_can("folder") end,
        enabled = function() return editable() and import_state ~= "picking" end,
        activate = function()
            if sh.mods.import_pick("folder") then
                import_state = "picking"
            end
        end,
    }
    rows[#rows + 1] = {
        kind = "action", id = "zip", label = "ui.mods.import.zip", help = "ui.mods.import.zip.help",
        visible = function() return sh.mods.import_can("zip") end,
        enabled = function() return editable() and import_state ~= "picking" end,
        activate = function()
            if sh.mods.import_pick("zip") then
                import_state = "picking"
            end
        end,
    }
    rows[#rows + 1] = note("ui.mods.import.picking", nil, { visible = function() return import_state == "picking" end })
    rows[#rows + 1] = note("ui.mods.import.drop", nil, { mobile = false })
    local dir = sh.mods.dir()
    rows[#rows + 1] = note("ui.mods.import.target")
    rows[#rows + 1] = note(dir ~= "" and dir or "<exe>/mods", nil, { cut = "middle" })
end

define_import = function()
    local rows = {}
    locked_note(rows)
    if import_state == "ready" then
        import_rows_found(rows)
    else
        import_rows_pick(rows)
    end
    sh.menu.define{
        id = IMPORT, title = "ui.mods.import.title", rows = rows,
        on_open = function()
            if import_state ~= "picking" then
                sh.mods.import_clear()
            end
            import_state = sh.mods.import_poll()
            define_import()
        end,
        -- The dialog answers some frames later (a drop at any time): the page shows the scan once it is there.
        on_update = function()
            local state, changed = sh.mods.import_poll()
            if changed then
                import_state = state
                define_import()
            end
        end,
    }
end

rebuild = function()
    built_editable = editable()
    local mods = sh.mods.list()
    local rows = {}
    locked_note(rows)
    if #mods == 0 then
        rows[#rows + 1] = note("ui.mods.none", nil, { cut = "wrap" })
        local dir = sh.mods.dir()
        rows[#rows + 1] = note(dir ~= "" and dir or "<exe>/mods", nil, { cut = "middle" })
    end
    for _, m in ipairs(mods) do
        mod_menu(m)
        rows[#rows + 1] = {
            kind = "submenu",
            id = m.id,
            -- Labels are literals here; the localized state is the row's help line.
            label = (m.state == "active" and "" or "- ") .. m.name, cut = "end",
            help = built_editable and "ui.mods.row." .. m.state or "ui.mods.locked",
            submenu = "options.mods." .. m.id,
        }
        -- On a phone, where pointing selects nothing, every mod's version and state stand on a note under it.
        rows[#rows + 1] = note("ui.mods.line." .. m.state, nil, { mobile = true, args = { version = m.version } })
    end
    rows[#rows + 1] = {
        kind = "submenu", id = "import", label = "ui.mods.import.title",
        help = built_editable and "ui.mods.import.help" or "ui.mods.locked", enabled = editable, submenu = IMPORT,
    }
    restart_rows(rows)
    -- The version a mod's "min_engine_version" is compared with; a name and a number, the same in every language.
    rows[#rows + 1] = note("Silent Hill PC " .. sh.engine.version)
    -- add_page keeps the link in "options" whatever order the files run in (options.lua sorts after this file) and
    -- when a mod defines "options" again; links other mods add to "options.mods.<id>" survive these rebuilds too.
    sh.menu.add_page{
        parent = "options", id = "options.mods", link_id = "mods", title = "ui.mods.title",
        label = "ui.mods.title", help = "ui.mods.help", rows = rows,
        -- The same Options tree serves the title and the pause menu: the rows follow where it was opened from.
        -- Built again on every visit: the rows follow where Options was opened from, and mods imported meanwhile (the
        -- console's modimport) show.
        on_open = function()
            rebuild()
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
define_import()

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
