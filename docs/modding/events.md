# Events: the bus sh.hooks

[Back to the guide](README.md)

There is one event bus per process. The game announces events; mods subscribe from Lua. `sh.hooks` exists in every script host — the game host (`scripts/main.lua`), the map hosts (`events.lua`, `hooks.lua`, `world_objects.lua`) and the character hosts — but not in the menu state.

The game builds an event only when somebody listens to it. Without mods the game does not change.

## Functions

| Function | What it does |
| --- | --- |
| `sh.hooks.on(event, fn[, {priority = 0, id = "name"}])` → handle | subscribes `fn(ev)` to an event. Subscribing again from the same mod with the same `event` and `id` in the same host replaces the function |
| `sh.hooks.off(handle)`, `sh.hooks.off(event, id)` → boolean | removes the mod's own handler |
| `sh.hooks.emit(event[, fields[, cancellable]])` → fields, cancelled | sends an event. Fields have string keys and values that are `nil`, booleans, numbers or strings. A mod names its own events `<mod id>:<name>` |
| `sh.hooks.list([event])` | an array of `{event, owner, id, priority, handle}` in call order |
| `sh.hooks.has(event)` | whether anybody is subscribed |

## The event object

`ev` lives only while the handler runs; keeping it and touching it later is the error `this event is over`.

- `ev.<field>` reads a field (`nil` when there is none); `ev.<field> = v` writes one.
- Only fields marked **M** in the catalogue can be changed. Writing a read-only field leaves the value and warns once in the log.
- `ev:cancel()` cancels the event, and so does `ev.cancelled = true`. Cancelling an event that is not marked **C** returns `false` and warns once.
- `ev.name`, `ev.cancellable` and `ev.cancelled` are properties. A field with such a name is reached with `ev:get(key)` and `ev:set(key, v)`; `ev:fields()` returns a copy of all fields.

## Order, errors, lifetime

- **Order of calls**: higher `priority` first; at equal priority the load order of the owner (the game first, then the mods); then the order of subscription. A cancel stops the delivery: handlers after the one that cancelled do not get the event. Whoever wants to decide first sets a higher `priority`.
- **Errors**: a handler that raises an error or runs out of its instruction budget is logged once — `hook <event> of <owner> error: ... (the handler is off)` — and removed. The event and the other handlers go on.
- **Lifetime**: handlers live as long as the host they were subscribed in. The game host lives for the whole session, so a handler from `main.lua` works on every map and checks `ev.map` itself. A map host is recreated on every map, and its handlers go with the map.

## The catalogue

Units as in the rest of the API: positions Q12 (4096 = 1 m, Y grows downwards), angles Q12, health and damage Q12 (409600 = 100). Names are short for the base game (`HandgunBullets`, `M2S00_HandgunBullets11`, `Groaner`) and full for mods (`mymod:Crowbar`). **C** — the event can be cancelled; **M** — the field can be changed.

| Event | C | Fields | When |
| --- | --- | --- | --- |
| `game.boot` | | — | the scripts of the game and the mods are loaded |
| `game.new_game` | | `difficulty` (`"easy"`, `"normal"`, `"hard"`) | a new game, before the first map |
| `game.load` | | `slot`, `quick` | a save was read, before its map loads |
| `game.save` | | `slot` (0 for a new entry), `quick` | before a save is written |
| `game.quick_save` | C | `map`, `instant` | the quick save key. Cancelled: no screen and no save |
| `game.quick_load` | C | `map`, `instant` | the quick load key. Cancelled: no screen and no load |
| `game.tick` | | `tick` | every logic tick (30 Hz), only in the game |
| `map.load` | | `map` | the map is loaded (after `on_map_load`) |
| `map.unload` | | `map` | leaving the map, before the next one loads |
| `map.room_enter` | | `map`, `room`, `prev_room` (−1 for the first room after a load) | Harry moved into another room of the map's grid |
| `map.event` | C | `map`, `handler` (a name of `handlers.json`), `index` | a map event handler is about to start. Cancelled: the handler does not run and its "done" flag is not set |
| `item.pickup` | C | `item`, `count` (M), `flag`, `map`, `x`, `z`, `y` | Harry starts picking an item up, before the question. Cancelled: the item stays |
| `item.picked` | | `item`, `count`, `flag`, `map` | the item was added to the inventory |
| `item.use` | C | `item` | an item is used from the inventory. Cancelled: the command does nothing |
| `item.given` | C | `item`, `count` (M, 0…255), `map`, `source` (`event`, `console`, `script`, `debug`) | an item is added without a pickup. Cancelled, or `count = 0`: nothing is added |
| `player.quick_heal` | C | `item`, `amount` (M), `health` | the quick heal key chose a healing item, before it is spent |
| `player.damage` | C | `amount` (M, before the difficulty scaling), `source`, `source_slot` | Harry is hit. Cancelled, or `amount = 0`: no health is lost |
| `player.death` | | `map` | Harry died |
| `entity.spawn` | | `chara`, `slot`, `x`, `z` | a character appeared in a slot, whatever spawned it |
| `entity.death` | | `chara`, `slot` | a character's health fell to zero |
| `flag.set` | | `flag`, `value` | an event flag changed |
| `menu.open` | | `menu` | a menu or a screen of the game opened (`options`, `inventory`, `map`, `save`, ...) |
| `cutscene.start` | | `id` | a cutscene started |
| `cutscene.end` | | `id` | control returned after a cutscene |
| `input.action` | C | `action` | an input action was pressed. Cancelled: the press is swallowed until release |

How some events are found, and what they do not cover:

- `entity.spawn`, `entity.death` and `flag.set` are found by comparing with the previous tick: they catch every source but arrive at the start of the next tick. `flag.set` is silent at a new game, a load and a continue.
- `input.action` covers only the keyboard and the mouse, and only actions that have a controller button of the original; a gamepad does not send it.
- For pickups made with `sh.world.pickup_create` the fields `x` and `z` of `item.pickup` may differ from the item's place; `sh.world.pickups()` has it.

## Two mods on one event

Both handlers run; the order is the load order, or the `priority`.

```lua
-- mods/alpha/scripts/main.lua
sh.hooks.on("map.room_enter", function(ev)
    if ev.map == "map2_s00" and ev.prev_room == -1 then sh.log("alpha: street") end
end, {id = "street"})

-- mods/beta/scripts/main.lua: runs before alpha whatever the load order
sh.hooks.on("map.room_enter", function(ev)
    if ev.map == "map2_s00" then sh.log("beta: room " .. ev.room) end
end, {id = "street", priority = 10})
```

Check in the console: `hooks map.room_enter` lists both handlers, `beta` first; `hooks_trace on` writes every event with its fields into the log.
