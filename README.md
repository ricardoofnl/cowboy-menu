# cowboy menu

A GoldHEN mod menu plugin for Red Dead Redemption 2 on PS4 (`CUSA08519`, game 1.32).
Open the menu in game with `R1 + O`. Built on the same verified native-call base as the
satchel-remover project.

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

The plugin resolves the running eboot base and calls RDR2 native handlers directly (located
by scanning the eboot for each native's 64-bit hash). Toggles are re-applied every frame from
a hooked game-thread tick; actions fire once on selection.

## Building

Built in CI (GitHub Actions) with the OpenOrbis toolchain and the GoldHEN SDK, which produces
`cowboy_menu.prx` as a build artifact. The `release` workflow cuts a tagged release with the
prx attached, taking a version input.

## Install

1. Copy `cowboy_menu.prx` to `/data/GoldHEN/plugins/` on the PS4.
2. Add it under the `[CUSA08519]` section of `/data/GoldHEN/plugins.ini`.
3. Enable the GoldHEN Plugin Loader and relaunch RDR2.

## Status

Early. Build pipeline and native base are up; UI, input and features are being built out from
the research, player menu and the satchel toggle first.