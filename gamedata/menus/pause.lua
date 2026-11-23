-- The ESC pause menu. Every `native` is registered by src/ui/pause/pause_menu.cpp: the action runs
-- the row, and the visible / enabled predicate of the same name decides whether it shows and is selectable
-- (SAVE is greyed while a boss fight or the notebook-only rule blocks it).
-- A mod adds a row with sh.menu.append_rows("pause", { ... }).

sh.menu.define{
    id = "pause",
    title = "ui.pause.paused",
    rows = {
        { id = "continue", label = "ui.pause.continue",      native = "pause.continue" },
        { id = "debug",    label = "ui.pause.debug_options", native = "pause.debug" },
        { id = "save",     label = "ui.pause.save",          native = "pause.save" },
        { id = "load",     label = "ui.pause.load",          native = "pause.load" },
        { id = "controls", label = "ui.pause.controls",      native = "pause.controls" },
        { id = "option",   label = "ui.pause.option",        native = "pause.option" },
        { id = "exit",     label = "ui.pause.exit",          native = "pause.exit",
          confirm = "ui.pause.return_to_title" },
    },
}
