# cowboy menu

A GoldHEN mod menu plugin for Red Dead Redemption 2 on PS4 (`CUSA08519`, game 1.32).
Open the menu in game with `R1 + O`. Built on the same verified native-call base as the
satchel-remover project.

Use D-pad up/down to select a row, left/right to change a value, and X to select or
cycle a value. O goes back. Money, weather, hour and minute selectors show the current
value in square brackets. After choosing a value, select Add/Remove Money, Apply Weather
or Apply Time to apply it.

## Menus

### Player
- god mode, never wanted, super run, super swim, super jump
- invisibility, slow motion, unlimited stamina, unlimited deadeye
- player ignore, self kill
- remove satchel (toggle)

### Weapon
- give all weapons to horse, give all ammo types
- equip semi-auto weapons, equip best weapons
- dual wield enabler / disabler, take all weapons
- infinite ammo, one hit kill, teleport gun, rapid fire, perfect accuracy, explosive bullet

### Horse
- unlimited stamina, god mode, invisibility, super run
- horse shop component, bonding level

## How it works

The plugin resolves the running eboot base and calls RDR2 native handlers at offsets verified
by scanning the 1.32 eboot for each native's 64-bit hash. A hooked game-thread tick applies
toggles; actions fire once on selection.

Unlimited stamina uses the engine's `InfiniteStamina` reset flag for both player and horse.
The flag is renewed each frame without refilling cores or stamina bars. Unlimited deadeye
uses `_MODIFY_INFINITE_TRAIL_VISION` (the same switch the built-in cheat phrase flips) plus
a core pin so the gauge stays full; it releases on toggle off. God mode blocks
damage through invincibility and entity proofs without repeatedly restoring health.
Horse protection follows the current mount, stays on that horse after dismounting, and falls
back to the saddle horse when no previous horse is available. Switching horses or disabling
the toggles clears protection from the previous horse.

## Building

With Docker running, build the plugin and run regression tests locally:

```sh
make docker
```

This prepares an amd64 OpenOrbis + GoldHEN build image, including on Apple Silicon, then
writes `dist/cowboy_menu.prx`. The first build downloads the toolchain; later builds reuse
the image cache. The compiler runs in a container with networking disabled.

The GitHub Actions `build` workflow also produces `cowboy_menu.prx` as an artifact.
The `release` workflow cuts a tagged release with the prx attached, taking a version input.

## Install

1. Copy `cowboy_menu.prx` to `/data/GoldHEN/plugins/` on the PS4.
2. Add it under the `[CUSA08519]` section of `/data/GoldHEN/plugins.ini`.
3. Enable the GoldHEN Plugin Loader and relaunch RDR2.

## Status

Early. Build pipeline and native base are up; UI, input and features are being built out from
the research, player menu and the satchel toggle first.
