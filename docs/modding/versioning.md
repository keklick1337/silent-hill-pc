# Engine version policy

[Back to the guide](README.md)

The engine has one version, `MAJOR.MINOR.PATCH`, kept in the [`VERSION`](../../VERSION) file at the root of the repository. A build adds metadata to it — the build number and the commit, `1.0.0+2890.1a2b3c4d5` — which only names the build and takes no part in comparisons.

Where the version is shown: the first line of the log, `SilentHillPC --version`, the console command `version`, the bottom of *Options > Mods*, and `sh.engine` in Lua.

## When which number grows

| Number | When | What it means for a mod |
| --- | --- | --- |
| `MAJOR` | a breaking change of what a mod sees: the format of `mod.json` or of a data file, the path or meaning of a key, the signature or behaviour of a Lua API function, an event of the bus | a mod made for the previous `MAJOR` may stop working; its author checks it and, if needed, sets `max_engine_version` |
| `MINOR` | additions that break nothing: a new function or event, a new key or kind of data, a new capability of the engine | old mods work as before; a mod that needs the new thing writes this version into `min_engine_version` |
| `PATCH` | fixes without changes of formats and the API | nothing changes; a `min_engine_version` with a `PATCH` is needed only when the mod depends on the fix |

## Rules for a mod

- Write into `min_engine_version` the engine version the mod was made and tested on, in full: `"1.2.0"`. `"1.2"`, `"v1.2.0"`, `">=1.2.0"`, `"1.x"` and a number instead of a string are manifest errors.
- Set `max_engine_version` only when you know the mod does not work on a newer engine. A `max_engine_version` below `min_engine_version` is a manifest error.
- When the requirement is not met the mod is not loaded, whatever its switch says: the mods screen shows it in the error state with the reason (`needs engine 1.2.0 or newer, this is 1.0.0`), and the same line goes to the log once and to the output of `mods`. Mods that list it in `dependencies` are switched off with the usual dependency error; mods that list it in `optional_dependencies` work without it.
- A script that has to work on several engine versions can ask:

```lua
sh.engine.version            -- "1.0.0"
sh.engine.major, sh.engine.minor, sh.engine.patch   -- numbers
sh.engine.build              -- "1.0.0+2890.1a2b3c4d5", for the log
if sh.engine.at_least(1, 2) then   -- engine 1.2.0 or newer
    -- what appeared in 1.2
end
```

The key `api` and `sh.api_version()` (the number of the script API) remain, but a new mod needs only `min_engine_version`: the engine version grows with every change of the API.

Examples: [`examples/mods/example_mod/`](../../examples/mods/example_mod/) declares `"min_engine_version": "1.0.0"`; [`examples/mods/needs_newer_engine/`](../../examples/mods/needs_newer_engine/) is a mod that is refused.

## For a release of the engine

The commit that is released raises the number in `VERSION` by the table above. The release tag `vX.Y.Z` must equal `VERSION`, or the release workflow and `tools/ci/package.sh` stop ([DEVELOPMENT.md](../DEVELOPMENT.md), section 5).
