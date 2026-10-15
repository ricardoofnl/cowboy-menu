# stamina and god mode

Target: CUSA08519, game 1.32

`SET_PED_RESET_FLAG(ped, 6, true)` enables `PRF_InfiniteStamina` for players and horses.
Reset flags expire each frame, so the game-thread tick renews the flag while enabled.
This replaces stamina restoration and core overpower calls. Core values are no longer
forced to 100 by these toggles; a full or golden core is not the indicator of protection.

God mode uses `SET_ENTITY_INVINCIBLE`, `SET_ENTITY_CAN_BE_DAMAGED` and
`SET_ENTITY_PROOFS`, plus `SET_PLAYER_INVINCIBLE` for the player. It blocks damage
without healing every frame. Self Kill clears player protection before applying damage.

Tracked entity handles allow cleanup after switching mounts, changing player ped,
or disabling a toggle on foot. Cleanup checks `DOES_ENTITY_EXIST` before calling
setters on an old handle. Horse protection stays on the last mount after dismounting;
when that handle is unavailable, the saddle horse is used.

## native verification

Offsets were resolved from the local decrypted eboot using the registration anchor
`48 b9 <hash> 48 8d 15 <relative handler>`, with virtual address = file offset - `0x4000`.

| Native | Hash | Handler offset |
| --- | --- | --- |
| SET_PED_RESET_FLAG | 0xC1E8A365BF3B29F2 | 0x2131fb0 |
| DOES_ENTITY_EXIST | 0xD42BD6EB2E0F1677 | 0x1e5a450 |

The reset flag handler reads three arguments: ped, flag index, boolean.
Flag 6 is documented in [ePedScriptResetFlags](https://github.com/Halen84/RDR3-Native-Flags-And-Enums/blob/main/ePedScriptResetFlags/README.md).
The three-argument proofs signature is documented in [rdr3-nativedb](https://github.com/alloc8or/rdr3-nativedb).

## validation

Run `make test` with a host C compiler. The native-call regression test covers toggle
independence, repeated frames without refills, switching horses, dismounting, missing
entities, player replacement, and Self Kill.

Console validation is still required: sprint, swim, gallop and jump with stamina enabled;
take bullet, fire, explosion and fall damage with god mode enabled; then disable each
toggle and check that normal stamina use and damage resume. Repeat after loading a save
and changing horses. The host test verifies calls and lifecycle, not game-engine behavior.
