# cowboy menu - implementation plan

Generated from the research workflow. Target CUSA08519 game 1.32. All native handler
offsets are resolved in `src/offsets.h` (anchor scan over the eboot).

## Architecture
COWBOY MENU - plugin architecture (fork the existing satchel-remover prx, do NOT start a new base)

Keep the whole prx scaffolding that is already proven on CUSA08519 1.32: module_start/module_stop, the sys_sdk_proc_info title+version fail-closed guard (GAME_TITLEID "CUSA08519", GAME_VERSION "1.32"), the GoldHEN Detour API, and the NativeCtx/nat_call native invoker in src/natives.h (ctx+0x00=ret slot, ctx+0x10=args array of 8-byte slots; floats bit-cast into a slot, ped/player handles in the low dword, return read as dword or float). Everything routes through this one nat_call path - it is the same scrNativeCallContext ABI as the EpineGUI invoker, so pick nat_call and drop the base's invoker.cpp.

HOOK POINT (single detour, script thread). Reuse the existing GET_PLAYER_PED handler detour. The game's scripts call GET_PLAYER_PED many times per frame on the script thread, which is the only thread where calling natives is safe. Replace the current "throttle every 32 calls" with a once-per-frame gate: in hk_get_player_ped, call Detour_Stub to run the original first, then read a monotonically increasing frame counter via GET_FRAME_COUNT and only run the menu body when the frame number changes. This collapses the many-calls-per-frame into exactly one tick/frame and gives us the per-frame entry the EpineGUI base calls _main(). Caveat to verify on-console: GET_PLAYER_PED only fires during active gameplay, so the menu will not draw on pause/load screens; if that matters, repoint a per-frame HUD/clock native instead (the ethylamine approach). Stay with one dedicated detour, not two.

PER-FRAME TICK (the heart of it):
  1. Detour_Stub original -> fills ctx->ret with the ped.
  2. f = GET_FRAME_COUNT(); if (f == lastFrame) return; lastFrame = f.  // once/frame
  3. player = PLAYER_ID(); ped = PLAYER_PED_ID() (re-fetch EVERY frame - handle changes on mount/dismount/respawn/mission).
  4. input: read open-combo; if open, DISABLE_CONTROL_ACTION on nav/accept/cancel + SET_INPUT_EXCLUSIVE, then read nav via IS_DISABLED_CONTROL_JUST_PRESSED.
  5. draw: if open, run the ported EpineGUI run() (banner + rows + scroller + indicator).
  6. apply active cheats: iterate the toggle state and re-apply each ON toggle's native(s) this frame; fire queued ACTION requests once.

MENU STATE (plain C, no heap). A small set of globals: int s_open; enum CurrentMenu {MAIN, PLAYER, WEAPON, HORSE, HORSE_SHOP}; int s_cursor; plus a flat block of toggle bools (god, satchel, neverWanted, superRun, superSwim, superJump, invis, slowMo, infStamina, infDeadeye, playerIgnore, dualWield, infAmmo, oneHitKill, tpGun, rapidFire, perfectAcc, explosiveBullet, horseStamina, horseGod, horseInvis, horseSuperRun) and scroller values (invisAlpha 0-255, timeScale 0.1-1.0, moveRate 1.0-2.0). ACTION requests (selfKill, giveAllWeapons, giveWeaponsToHorse, giveAllAmmo, equipSemiAuto, equipBest, takeAllWeapons, horseBonding, horseShopApply) set a one-shot flag that the tick consumes and clears.

TOGGLE vs ACTION driving:
  - TOGGLE = re-apply its native(s) every frame while the bool is on (the engine/scripts reset most of these each frame). On the transition to OFF, run the undo path exactly once (track prevState per toggle): e.g. god off -> SET_ENTITY_INVINCIBLE(ped,false), SET_ENTITY_CAN_BE_DAMAGED(ped,true), SET_ENTITY_PROOFS(ped,0,...); slow-mo off -> SET_TIME_SCALE(1.0); invis off -> SET_ENTITY_VISIBLE(ped,true)/RESET_ENTITY_ALPHA; dual-wield is a single toggle calling _SET_ALLOW_DUAL_WIELD(ped,on) on change (expose "enabler"/"disabler" as the two sides of one bool, or two action buttons).
  - ACTION = fire once on ACCEPT. Give-weapons loops a hardcoded weapon-hash table; teleport-gun/self-kill/bonding are single calls.

MOUNT FEATURES. Resolve the mount each frame: guard IS_PED_ON_MOUNT(ped) then mount = GET_MOUNT(ped) (or _GET_SADDLE_HORSE_FOR_PLAYER for on-foot effects); mount==0 means skip. Re-apply after remount because the mount entity handle changes. Horse toggles re-apply like player toggles; bonding/shop are actions.

SATCHEL TOGGLE. The existing strip logic (IS_META_PED_USING_COMPONENT -> REMOVE_TAG_FROM_META_PED -> _UPDATE_PED_VARIATION -> _SET_ACTIVE_META_PED_COMPONENTS_UPDATED) becomes a Player-menu toggle gated by s_satchel: only run it when the toggle is on. No second hook - it folds into the same tick. Keep the _SET_META_PED_TAG->empty coat fallback available.

NON-NATIVE DATA TABLES. GIVE_WEAPON_TO_PED, ammo-by-type, instant-kill-by-group, firing-pattern all need joaat hashes of string names (WEAPON_*, AMMO_*, weapon-group, FIRING_PATTERN_FULL_AUTO). These are NOT natives and are not in the native DB as handlers - ship a hardcoded weapon/ammo/group hash table as a separate header (compute joaat at build time or hardcode known values). This table is required for every "give/equip" feature.

FILE LAYOUT (incremental on current src/): offsets.h (add all newly resolved handler offsets), natives.h (keep nat_call; add thin wrappers per native), menu.h/menu.c (ported EpineGUI draw+nav+toggle, in C), features_player.c / features_weapon.c / features_horse.c (per-category apply functions), weapons_table.h (joaat tables), main.c (hook + once/frame tick + dispatch).

## UI and input
UI + INPUT PLAN - reuse a base, render through the game's own natives (honest: this is the hardest, most on-console-iterative part of the whole project).

REUSE, DON'T BUILD FROM SCRATCH. Port the EpineGUI menu from ethylamine/PS4-RDR-2-Menu-Base (maintained fork a0zhar/PS4-RDR2-Menu) - specifically rdr2Payload/source/menu.cpp + include/menu.h (the class: banner, option, toggle(bool*), scroller(int/float/string), submenu, data, drawText, monitorButtons/run) and the control enum in include/buttons.h. This is the exact lineage behind the closed-source Havana prx, so it is known-good on RDR2. DISCARD the entire loader layer (source/main.c, proc.c, kern.c, gta.c, embed.s, gtaPayload.bin, crt0.s, linker.x, syscall-gadget lib.cpp) - our prx already runs in-process with native calls and a per-frame hook, so none of the payload-injection machinery is needed. Only the CODE ports; do NOT copy the base's offsets, syscall gadget, or GTAVars addresses (it targets US CUSA03041 1.29). Translate the C++ menu class to C or compile it as C++ against our nat_call - either is fine; nav/toggle logic is tiny.

RENDER METHOD = game RAGE/scaleform draw natives every frame on the script thread (NOT a framebuffer/flip hook, NOT ImGui, NOT OrbisGL). This is why the menu matches RDR2's look and sits inside the frame, and it is what lets us suppress the game reacting to our buttons. Per the base's menu.cpp:
  - background/rows/scrollbar: GRAPHICS::DRAW_RECT(x,y,w,h, r,g,b,a, ...) in normalized 0..1 coords.
  - text: build a scaleform buffer <TEXTFORMAT><FONT FACE=.. COLOR=#.. SIZE=..><P ALIGN=..>text</P></FONT></TEXTFORMAT>, wrap with MISC::VAR_STRING(10,"LITERAL_STRING",...) then VAR_STRING(42,"COLOR_STRING",...), submit via UIDEBUG::_BG_DISPLAY_TEXT(str,x,y).
  - notifications: UILOG::_UILOG_SET_CACHED_OBJECTIVE / _PRINT_CACHED_OBJECTIVE. feedback sound: AUDIO::PLAY_SOUND_FRONTEND.
Honest risk: _BG_DISPLAY_TEXT and the VAR_STRING path must exist and be resolved at 1.32; if _BG_DISPLAY_TEXT differs, fall back to a plain HUD text native. Text positioning, sizing and color all need eyeballing on-console - budget real iteration here.

OPEN COMBO R1 + O (as the user asked). While closed, each frame: IS_CONTROL_PRESSED(0, INPUT_FRONTEND_RB) && IS_CONTROL_PRESSED(0, INPUT_FRONTEND_CANCEL). Control-index arg values (build-independent, from femga/rdr3_discoveries - these are native ARGUMENTS, not handlers to resolve): INPUT_FRONTEND_RB=0x17BEC168 (R1), INPUT_FRONTEND_CANCEL=0x156F7119 (circle/O). CAVEAT the research flags: circle is not disabled while the menu is closed, so R1+O can also fire the game's own back/cancel in some screens - consume the input on the frame we open, or fall back to the base's RB+RS (right-stick click) if the conflict is annoying. Ship R1+O, keep RB+RS as a compile-time fallback.

NAVIGATION while open (each frame: DISABLE_CONTROL_ACTION(0, <each>, true) on UP/DOWN/LEFT/RIGHT/ACCEPT/CANCEL + SET_INPUT_EXCLUSIVE so the game ignores them, then read with IS_DISABLED_CONTROL_JUST_PRESSED):
  UP 0x6319DB71 / DOWN 0x05CA7C52  -> move cursor
  LEFT 0xA65EBAB4 / RIGHT 0xDEB34313 -> adjust scrollers (alpha, time-scale, move-rate)
  ACCEPT (cross) 0xC7B5340A -> toggle on/off or enter submenu / fire action  (this is the "X to toggle" the user wants)
  CANCEL (circle) -> back one level, or close at the root
EpineGUI::toggle(bool*) already flips the bool on ACCEPT and prints On/Off, so toggles come for free.

LOOK / LAYOUT. Banner reads "cowboy menu" (western styling - amber/cream on dark, matches RDR2 UI). Root list: Player, Weapon Options, Horse Options. Each submenu is a vertical list; toggle rows show "On"/"Off" on the right, scroller rows show the value, submenu rows show a ">" chevron, actions are plain rows. A highlight bar marks the cursor row; a right-edge indicator shows scroll position when the list overflows. Player menu order: God Mode, Remove Satchel, Never Wanted, Super Run, Super Swim, Super Jump, Invisibility, Slow Motion, Unlimited Stamina, Unlimited Deadeye, Player Ignore, Self Kill (action). Weapon and Horse menus follow their feature lists; Horse Options contains a "Horse Shop" sub-list for tack.

UI NATIVE HANDLERS TO RESOLVE (hashes come from alloc8or/rdr3-nativedb natives.json first, then anchor-scan the eboot for the offset): GET_FRAME_COUNT, DRAW_RECT, VAR_STRING, _BG_DISPLAY_TEXT, _UILOG_SET_CACHED_OBJECTIVE, _PRINT_CACHED_OBJECTIVE, PLAY_SOUND_FRONTEND, IS_CONTROL_PRESSED, IS_DISABLED_CONTROL_PRESSED, IS_DISABLED_CONTROL_JUST_PRESSED, DISABLE_CONTROL_ACTION, SET_INPUT_EXCLUSIVE. These ~12 are the gating item for first light on screen.

### render + input specifics

- render: The game's own RAGE/scaleform draw natives, called every frame from the script thread. This is confirmed in the base's menu.cpp: it draws the banner, body, scroller and indicator bars with GRAPHICS::DRAW_RECT(x,y,w,h, r,g,b,a, ...) using normalized 0..1 coordinates, and draws every text line with a scaleform-formatted string - it builds a <TEXTFORMAT><FONT FACE=... COLOR=#... SIZE=...><P ALIGN=...>text</P></FONT></TEXTFORMAT> buffer, wraps it with MISC::VAR_STRING(10,"LITERAL_STRING",...) then VAR_STRING(42,"COLOR_STRING",...), and submits it via UIDEBUG::_BG_DISPLAY_TEXT(str,x,y). Notifications use UILOG::_UILOG_SET_CACHED_OBJECTIVE/_PRINT_CACHED_OBJECTIVE, sounds use AUDIO::PLAY_SOUND_FRONTEND. It does NOT hook sceVideoOutSubmitFlip/flip, does NOT blit to the framebuffer, and does NOT use a 2D helper (OrbisGL) or RAGE's own GUI widgets. Rendering through the game's natives is why the menu matches RDR2's look and sits correctly in the frame; a flip-hook/ImGui overlay would draw outside the game pipeline, couldn't disable the game's own control actions, and is far more work - not recommended here. You must resolve the handler offsets for DRAW_RECT, VAR_STRING, _BG_DISPLAY_TEXT, GET_FRAME_COUNT, the UILOG + AUDIO natives for CUSA08519 1.32 (anchor scan + rdr3-nativedb hashes).

- input: Read input through the game's RAGE PAD control natives, not scePad - this is what the base does and it integrates with the engine so you can suppress the game reacting to the same buttons. To OPEN while closed: PAD::IS_CONTROL_PRESSED(0, INPUT_FRONTEND_RB) && IS_CONTROL_PRESSED(0, INPUT_FRONTEND_<second>). The base ships RB+RS; for the requested R1 + O just use INPUT_FRONTEND_RB (R1, control hash 0x17BEC168) + INPUT_FRONTEND_CANCEL (circle/O, 0x156F7119). While OPEN: each frame call PAD::DISABLE_CONTROL_ACTION(0, INPUT_FRONTEND_UP/DOWN/LEFT/RIGHT/ACCEPT/CANCEL/X, true) and SET_INPUT_EXCLUSIVE, then read navigation with PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, INPUT_FRONTEND_UP/DOWN) to move the cursor, INPUT_FRONTEND_LEFT/RIGHT (0xA65EBAB4/0xDEB34313) for scrollers, INPUT_FRONTEND_ACCEPT (cross, 0xC7B5340A) to toggle/select (this is the "X to toggle" the user wants), and INPUT_FRONTEND_CANCEL to go back/close. EpineGUI::toggle(bool*) already flips a bool on ACCEPT and prints On/Off. scePad (libScePad scePadGetHandle(user 0)+scePadReadState) is a viable fallback from a prx but bypasses the control system so you'd have to manually eat the inputs - only use it if you need to read the pad before the script system is up. Resolve PAD native handler offsets (IS_CONTROL_PRESSED, IS_DISABLED_CONTROL_JUST_PRESSED, IS_DISABLED_CONTROL_PRESSED, DISABLE_CONTROL_ACTION, SET_INPUT_EXCLUSIVE) for 1.32 EU; the INPUT_FRONTEND_* values above are build-independent control-index hashes verified from femga/rdr3_discoveries.

- draw loop: Hook one RDR2 native handler that the game invokes once per rendered frame on the script thread, and run draw+input+active-cheats inside it - exactly the mechanism the satchel-remover already uses. In the base, the per-frame entry is _main() (main.cpp): it gates on MISC::GET_FRAME_COUNT() increasing so the body runs exactly once per frame, then calls menu.monitorButtons() and, if open, menu.run(). Mirror that in the prx: install a Detour (GoldHEN Detour_DetourFunction, same as the current OFF_GET_PLAYER_PED hook) on a per-frame handler; call Detour_Stub to let the original run; then run your GET_FRAME_COUNT-gated tick. Two concrete hook-point choices, both proven on the script thread: reuse the existing GET_PLAYER_PED handler (already hooked and firing many times/frame - just gate with GET_FRAME_COUNT), or repoint a per-frame HUD/clock native the way ethylamine does (their loader patches the GET_CLOCK_DAY_OF_MONTH registration lea to redirect that handler to their nativeHook). Recommended: keep a dedicated detour on a stable per-frame native, gate once-per-frame, and fold the satchel strip in as a Player-menu toggle rather than a second independent hook. All cheat natives (god mode re-applied each frame, infinite ammo, etc.) are called from this same tick, which is why they are thread-safe.

- open toggle: R1 + O == PAD::IS_CONTROL_PRESSED(0, INPUT_FRONTEND_RB) && PAD::IS_CONTROL_PRESSED(0, INPUT_FRONTEND_CANCEL). INPUT_FRONTEND_RB=0x17BEC168 (R1), INPUT_FRONTEND_CANCEL=0x156F7119 (circle/O). Navigation once open: UP=0x6319DB71 / DOWN=0x05CA7C52 (cursor), LEFT=0xA65EBAB4 / RIGHT=0xDEB34313 (scrollers), ACCEPT=0xC7B5340A (cross, toggle/select), CANCEL (back/close). Caveat: because circle (CANCEL) is NOT disabled while the menu is closed, an R1+O press can also fire the game's own cancel/back in some contexts; consume the inputs on the frame you open, or keep the base's RB+RS if that conflict is annoying.

## Per-feature implementation
PER-FEATURE IMPLEMENTATION (native(s), toggle/action, re-apply strategy). All toggles re-apply every frame while on; all mount features re-resolve the mount and re-apply after remount. Underscore names are community labels - the HASHES are canonical, parameter semantics flagged are the uncertain part.

HELPERS (every tick): player=PLAYER_ID(); ped=PLAYER_PED_ID() (refetch always); for horse: guard IS_PED_ON_MOUNT(ped) then mount=GET_MOUNT(ped), or _GET_SADDLE_HORSE_FOR_PLAYER(player) for on-foot horse effects.

PLAYER
- God Mode (TOGGLE): each frame SET_PLAYER_INVINCIBLE(player,true), SET_ENTITY_INVINCIBLE(ped,true), SET_ENTITY_CAN_BE_DAMAGED(ped,false), optional SET_ENTITY_PROOFS(ped,0xFFFFFFFF,true). OFF: same with false/true/0. Clear god before Self Kill.
- Remove Satchel (TOGGLE): gate the existing strip chain - if IS_META_PED_USING_COMPONENT(ped,0x94504D26) then REMOVE_TAG_FROM_META_PED(ped,0x94504D26,0), _UPDATE_PED_VARIATION(ped,0,0,0,0,0), _SET_ACTIVE_META_PED_COMPONENTS_UPDATED(ped,0). Self-limiting; re-applies after wardrobe reload. _SET_META_PED_TAG->empty is the coat-safety fallback.
- Never Wanted (TOGGLE): on-enable once SET_MAX_WANTED_LEVEL(0) + _SET_DISABLE_PLAYER_WANTED_LEVEL(player,true); each frame CLEAR_WANTED_SCORE(player) (RDR2 uses a wanted SCORE, not GTA stars); nuclear add _SET_LAW_DISABLED(true). OFF: restore max level, disable=false, law=false. One-shot wipes: CLEAR_PLAYER_WANTED_LEVEL, CLEAR_PLAYER_PAST_CRIMES.
- Super Run (TOGGLE): each frame SET_PED_MOVE_RATE_OVERRIDE(ped, ~1.5) (NO GTA SET_RUN_SPRINT_MULTIPLIER in RDR2; resets each frame; keep modest).
- Super Swim (TOGGLE): each frame SET_SWIM_MULTIPLIER_FOR_PLAYER(player,1.49) (game clamps ~1.0-1.49).
- Super Jump (TOGGLE): each frame SET_SUPER_JUMP_THIS_FRAME(player) (MISC namespace, strictly per-frame; weak in RDR2, verify).
- Invisibility (TOGGLE): each frame SET_ENTITY_VISIBLE(ped,false) OR SET_ENTITY_ALPHA(ped, scrollerAlpha, false). OFF: SET_ENTITY_VISIBLE(ped,true)/RESET_ENTITY_ALPHA(ped).
- Slow Motion (TOGGLE): each frame SET_TIME_SCALE(scroller 0.3) (MISC; resets to 1.0). OFF: SET_TIME_SCALE(1.0).
- Unlimited Stamina (TOGGLE): each frame RESTORE_PLAYER_STAMINA(player,1.0) + _SET_ATTRIBUTE_CORE_VALUE(ped,1,100) (core 1=stamina, verify). Lighter: _SET_PLAYER_STAMINA_SPRINT_DEPLETION_MULTIPLIER(player,0.0)+SET_PLAYER_STAMINA_RECHARGE_MULTIPLIER(player,high).
- Unlimited Deadeye (TOGGLE): each frame _SPECIAL_ABILITY_RESTORE_OUTER_RING(player,max) or _SPECIAL_ABILITY_RESTORE_BY_AMOUNT(player,max,0,0,0) + _SET_ATTRIBUTE_CORE_VALUE(ped,2,100) (core 2=deadeye, verify). Read with _GET_PLAYER_DEAD_EYE/_GET_PLAYER_MAX_DEAD_EYE to only top up.
- Player Ignore (TOGGLE): on-enable + periodically SET_EVERYONE_IGNORE_PLAYER(player,true) (NO SET_POLICE_IGNORE_PLAYER in RDR2; pair with _SET_LAW_DISABLED(true)). OFF: false.
- Self Kill (ACTION): clear god first, then SET_ENTITY_HEALTH(ped,0,0), or APPLY_DAMAGE_TO_PED(ped,10000,true,0,ped) to register a death.

WEAPON (give/equip loop over a hardcoded WEAPON_*/AMMO_*/group joaat table - these string hashes are NOT natives, ship them in weapons_table.h)
- Give All Weapons (ACTION): loop table GIVE_WEAPON_TO_PED(ped,wHash,500,false,false,0,true,0.5,1.0,0,false,0.0,false) (RDR2 13-arg sig, not GTA 5-arg), then SET_PED_AMMO to top up.
- Give All Weapons to Horse (ACTION): mount=GET_MOUNT(ped); for each LONGARM hash _ATTACH_WEAPON_TO_HORSE_HOLSTER(mount,wHash,ped) (2 longarm slots only; pistols belong on player). _DELETE_WEAPON_OBJECTS_ON_HORSE clears. UNCERTAIN for non-longarms.
- Give All Ammo Types (ACTION): per ammo-type hash _ENABLE_ALL_SPECIAL_AMMO_FOR_PED(ped,wHash) then SET_PED_AMMO_BY_TYPE(ped,ammoType,max) or _ADD_AMMO_TO_PED_BY_TYPE. Map weapon->ammo with GET_PED_AMMO_TYPE_FROM_WEAPON.
- Equip Semi-Auto (ACTION): give + SET_CURRENT_PED_WEAPON(ped,hash,true,0,false,false) over a hardcoded semi-auto list (no native classifies semi-auto).
- Equip Best Weapons (ACTION): h=GET_BEST_PED_WEAPON(ped,false,false); SET_CURRENT_PED_WEAPON(ped,h,true,0,false,false). Per-group: _GET_BEST_PED_WEAPON_IN_GROUP.
- Dual Wield Enabler/Disabler (TOGGLE, one bool two faces): _SET_ALLOW_DUAL_WIELD(ped,on) on change; GET_ALLOW_DUAL_WIELD reflects UI.
- Take All Weapons (ACTION): REMOVE_ALL_PED_WEAPONS(ped,true,true).
- Infinite Ammo (TOGGLE): SET_PED_INFINITE_AMMO(ped,true,0) + _SET_PED_INFINITE_AMMO_CLIP(ped,true) on change (both false OFF). (GTA SET_PED_INFINITE_AMMO_CLIP absent; use the underscore one.)
- One Hit Kill (TOGGLE): preferred _SET_PLAYER_WEAPON_GROUP_AS_INSTANT_KILL(player,group,true) looped over groups; simpler SET_PLAYER_WEAPON_DAMAGE_MODIFIER(player,1000.0) (OFF 1.0). Takes PLAYER index.
- Teleport Gun (TOGGLE): each frame if GET_PED_LAST_WEAPON_IMPACT_COORD(ped,&c) then SET_ENTITY_COORDS(ped,c.x,c.y,c.z,false,false,false,false); clear so it fires once/shot. Precise variant via START_SHAPE_TEST_LOS_PROBE+GET_SHAPE_TEST_RESULT.
- Rapid Fire (TOGGLE, BEST-EFFORT): SET_PED_SHOOT_RATE(ped,1000)+SET_PED_FIRING_PATTERN(ped,FIRING_PATTERN_FULL_AUTO). STRONGLY UNCERTAIN - these govern AI firing, likely do not speed a human trigger. Flag in UI as experimental.
- Perfect Accuracy (TOGGLE): _SET_PLAYER_TOTAL_ACCURACY_MODIFIER(player,100.0); NPC SET_PED_ACCURACY(ped,100). UNCERTAIN for manual aim (affects auto-aim/deadeye spread).
- Explosive Bullet (TOGGLE): per frame read GET_PED_LAST_WEAPON_IMPACT_COORD and ADD_OWNED_EXPLOSION(ped,x,y,z,type,scale,audible,invis,shake) at impact, clear last-damage so once/shot. Proper route: _ENABLE_ALL_SPECIAL_AMMO_FOR_PED + fill explosive AMMO_* type. _SET_PLAYER_EXPLOSIVE_WEAPON_DAMAGE_MODIFIER only scales, does not add.

HORSE (all target the MOUNT ped, re-apply after remount)
- Unlimited Stamina Horse (TOGGLE): each tick _SET_ATTRIBUTE_CORE_VALUE(mount,1,100); for never-drain _ENABLE_ATTRIBUTE_CORE_OVERPOWER(mount,1,1.0,false) once (re-apply if lapses).
- God Mode Horse (TOGGLE): SET_ENTITY_INVINCIBLE(mount,true) on change; belt-and-braces each tick _SET_ATTRIBUTE_CORE_VALUE(mount,0,100) + SET_ENTITY_HEALTH(mount,GET_ENTITY_MAX_HEALTH(mount,false),0); optional SET_ENTITY_MAX_HEALTH. OFF invincible=false.
- Horse Shop Component (ACTION): _APPLY_SHOP_ITEM_TO_PED(mount,tackHash,true,false,false); remove category REMOVE_SHOP_ITEM_FROM_PED_BY_CATEGORY(mount,catHash,0,false) (prefer this over _REMOVE_SHOP_ITEM_FROM_PED which is build 1355); commit _REFRESH_META_PED_SHOP_ITEMS(mount,1) + _UPDATE_PED_VARIATION(mount,0,0,0,0,0) + _SET_ACTIVE_META_PED_COMPONENTS_UPDATED(mount,0). REQUIRES real saddle/tack joaat component hashes - NOT in the native DB, source separately.
- Invisibility Horse (TOGGLE): SET_ENTITY_VISIBLE(mount,false) hard, or SET_ENTITY_ALPHA(mount,0,false) soft; OFF SET_ENTITY_VISIBLE(mount,true)/RESET_ENTITY_ALPHA(mount).
- Super Run Horse (TOGGLE+boost): each tick SET_PED_MOVE_RATE_OVERRIDE(mount,value) (clamped 0.0-1.15, modest) + permanently SET_ATTRIBUTE_BASE_RANK(mount,5,max) PA_SPEED and (mount,6,max) PA_ACCELERATION; timed burst BOOST_PLAYER_HORSE_SPEED_FOR_TIME(player,boost,durMs) re-fired on interval.
- Bonding Level (ACTION, max=4): _SET_MOUNT_BONDING_LEVEL(mount,4) (authoritative) + COMPENDIUM_HORSE_BONDING(mount,4) for UI; alt SET_ATTRIBUTE_BASE_RANK(mount,7,max) PA_BONDING.

## Milestones

- M0 Toolchain + fork: confirm the OpenOrbis+GoldHEN build already produces the current satchel_remover.prx in CI; rename target to cowboy_menu.prx; vendor the EpineGUI menu.cpp/menu.h/buttons.h from a0zhar/PS4-RDR2-Menu into src/ (discard the entire loader layer). No behavior change yet - just compiles.
- M1 Resolve the UI/PAD/MISC/GRAPHICS native handler offsets FIRST (this is the gate for anything on screen): pull the ~12 UI hashes from rdr3-nativedb natives.json, then anchor-scan bin/eboot.bin (48 b9 <hash>; 48 8d 15 <disp32>; handler=rip_after_lea+disp32) exactly as notes/natives.md documents. Add them to offsets.h. Parallel agent task.
- M2 Per-frame tick: convert the GET_PLAYER_PED hook from the every-32-calls throttle to a GET_FRAME_COUNT once-per-frame gate; prove it fires once/frame on the script thread (log a counter, or flash a DRAW_RECT).
- M3 First light: draw a single DRAW_RECT + one scaleform text line via VAR_STRING/_BG_DISPLAY_TEXT. This is the riskiest on-console step - iterate position/size/color/font until a labeled box renders inside the RDR2 frame. Fallback to a HUD text native if _BG_DISPLAY_TEXT misbehaves at 1.32.
- M4 Input + open/close: implement R1+O open (IS_CONTROL_PRESSED RB+CANCEL), DISABLE_CONTROL_ACTION+SET_INPUT_EXCLUSIVE while open, nav via IS_DISABLED_CONTROL_JUST_PRESSED UP/DOWN/ACCEPT/CANCEL. Prove cursor moves and the game does not react to the same buttons; handle the circle-also-cancels caveat (consume on open or fall back to RB+RS).
- M5 Menu framework: port EpineGUI banner/option/toggle/scroller/submenu/run; build the root list (Player/Weapon/Horse) and empty submenus with working navigation and On/Off rendering. No cheats wired yet.
- M6 Satchel toggle (first real feature, lowest risk - natives already resolved and verified): wire Remove Satchel in the Player menu to gate the existing strip chain. Validates the whole toggle->apply loop end to end.
- M7 Player menu cheats: resolve the player gameplay native offsets (anchor-scan), implement god/never-wanted/super-run/swim/jump/invis/slow-mo/stamina/deadeye/ignore toggles + self-kill action, each with its OFF-path. Verify god-blocks-selfkill ordering.
- M8 Weapon menu: ship the hardcoded WEAPON_*/AMMO_*/group joaat table, resolve weapon native offsets, implement give/equip actions and infinite-ammo/one-hit-kill/teleport-gun/explosive-bullet toggles. Mark rapid-fire and perfect-accuracy experimental.
- M9 Horse menu: resolve horse native offsets, implement mount resolution + stamina/god/invis/super-run toggles and bonding action; source real tack joaat hashes before enabling Horse Shop (leave shop as a stub until hashes are confirmed).
- M10 Polish + harden: western styling pass on the banner/colors, feedback sounds via PLAY_SOUND_FRONTEND, notifications via UILOG, confirm every toggle's OFF-path restores cleanly, re-verify fail-closed on title/version, package prx + plugins.ini instructions.

## Risks

- Everything is pinned to CUSA08519 game 1.32 EU: native HASHES are build-independent but every HANDLER OFFSET must be re-resolved for this eboot via the anchor scan. Any game update breaks all offsets - keep the existing fail-closed title+version guard.
- The reusable EpineGUI base targets US CUSA03041 1.29 / Havana 1.32 - only the CODE ports. Do NOT copy its offsets, syscall gadget, or GTAVars/Payload addresses.
- UI is the hard part and needs real on-console iteration: scaleform text (VAR_STRING + _BG_DISPLAY_TEXT) positioning/sizing/color is eyeballed, and _BG_DISPLAY_TEXT may differ at 1.32 - have a HUD-text fallback. Nothing in the menu is verifiable without a console.
- R1+O caveat: circle (INPUT_FRONTEND_CANCEL) is not disabled while the menu is closed, so the open combo can also fire the game's own back/cancel in some screens. Consume the input on open or fall back to the base's RB+RS.
- Native calls are only safe on the script thread. All draw/input/cheat calls must run from the per-frame native-handler hook, never a raw pthread.
- The GET_PLAYER_PED hook only ticks during active gameplay, so the menu will not draw on pause/load screens. If that is unacceptable, repoint a per-frame HUD/clock native instead - costs extra RE.
- ~12 UI/PAD/GRAPHICS/MISC native hashes were NOT provided and must be pulled from rdr3-nativedb before their offsets can be scanned - a prerequisite for M1/M3/M4.
- Several feature semantics are uncertain and need in-game verification: attribute core index mapping (0=health,1=stamina,2=deadeye), _SPECIAL_ABILITY_RESTORE_BY_AMOUNT trailing args, rapid-fire (SET_PED_SHOOT_RATE likely only affects AI, not a human trigger), perfect-accuracy (affects auto-aim/deadeye, not manual aim), and whether horse god mode needs the health-core pin on top of invincible.
- give/equip features depend on a hardcoded WEAPON_*/AMMO_*/weapon-group joaat table that is NOT in the native DB - must be sourced/computed separately or those features are inert.
- Horse Shop needs real saddle/tack joaat component hashes that are not in the native DB; without them the shop feature cannot apply anything. _REMOVE_SHOP_ITEM_FROM_PED is build 1355 and may be absent on 1.32 - use REMOVE_SHOP_ITEM_FROM_PED_BY_CATEGORY and fail-closed on any hash with no resolved handler.
- Satchel coat-safety caveat still applies when exposed as a toggle: plain removal may deform some coats; keep the _SET_META_PED_TAG->empty fallback, and some coats bake the strap into the mesh so it will persist.

## Open questions

- Should the menu draw on pause/load screens? If yes we need a different per-frame hook point than GET_PLAYER_PED (which only fires in active gameplay) - confirm before M2.
- R1+O vs RB+RS as the shipped open combo: accept the circle-also-cancels quirk and consume the input, or default to the base's RB+RS? Need a call before M4.
- What are the actual saddle/tack joaat component hashes for the Horse Shop feature, and which categories? Not in the native DB - where do we source them (horse metaped component dump / known tack table)?
- Confirm the attribute core index mapping on-console (0=health, 1=stamina, 2=deadeye) and the ePedAttribute indices (PA_SPEED=5, PA_ACCELERATION=6, PA_BONDING=7) before relying on stamina/deadeye/horse features.
- Does _BG_DISPLAY_TEXT exist and behave at 1.32 EU, or do we need the HUD-text fallback from first light? Determines M3 risk.
- Rapid Fire and Perfect Accuracy are likely ineffective for a human player - keep them as experimental toggles, drop them, or implement a per-frame fire-input force instead?
- Scope of 'give all weapons': full hardcoded weapon table, or a curated set? And should horse-holster give be longarms-only (the game's real constraint) with a note, or attempt all?
- Confirm the exact 12 UI/PAD/MISC/GRAPHICS native hashes from rdr3-nativedb (GET_FRAME_COUNT, DRAW_RECT, VAR_STRING, _BG_DISPLAY_TEXT, UILOG pair, PLAY_SOUND_FRONTEND, the 4 PAD control natives, SET_INPUT_EXCLUSIVE) - these are the only missing hashes blocking the build.
- Does the user want per-toggle persistence across respawns/save-reloads, or is per-session state fine? Affects how aggressively OFF-paths and re-apply run.
- Confirm the dist/ naming and plugins.ini section stay [CUSA08519] and the prx is renamed cowboy_menu.prx without breaking the existing CI release workflow.

## Reference tick (from research)
```c
Per-frame tick inside the prx (C, reusing the project's existing nat_call/NativeCtx in src/natives.h):

// resolved per-build handler offsets (anchor scan, see notes/natives.md)
static unsigned s_lastFrame = 0; static int s_open = 0;
static int s_godmode = 0, s_noSatchel = 0, s_infAmmo = 0; // toggle bools

static void hk_per_frame(NativeCtx *ctx) {
    Detour_Stub(&g_frameDetour, frame_fn_t, ctx);     // let original run first
    unsigned f = (unsigned)nat_call(g_base, OFF_GET_FRAME_COUNT, 0, 0);
    if (f == s_lastFrame) return;                       // once per frame only
    s_lastFrame = f;

    uint64_t rb[1]={CTRL_RB}, ci[1]={CTRL_CANCEL};
    int r1 = nat_call(g_base, OFF_IS_CONTROL_PRESSED, (uint64_t[]){0,CTRL_RB},2);
    int o  = nat_call(g_base, OFF_IS_CONTROL_PRESSED, (uint64_t[]){0,CTRL_CANCEL},2);
    if (!s_open && r1 && o) s_open = 1;                 // R1 + O opens

    monitor_buttons_and_draw();                         // ported EpineGUI run()

    int ped = (int)nat_call(g_base, OFF_GET_PLAYER_PED, (uint64_t[]){0xFF},1);
    if (s_godmode && ped) nat_call(g_base, OFF_SET_ENTITY_INVINCIBLE,(uint64_t[]){(uint32_t)ped,1},2);
    if (s_noSatchel && ped && gh_is_using_satchel(g_base,ped)) { gh_remove_satchel(..); ... }
}

Menu build (ported EpineGUI, C++ in the base):
void playerMenu(){ menu.banner("Player");
  menu.option("God Mode"); menu.toggle(&s_godmode);
  menu.option("Remove Satchel"); menu.toggle(&s_noSatchel);
  menu.option("Super Run"); menu.toggle(&s_superRun);
  menu.option("Weapon Options"); menu.submenu(weaponMenu);
  menu.option("Horse Options"); menu.submenu(horseMenu); }

The EpineGUI invoke<Ret>(handlerAddr, args...) template (invoker.h) and the project's nat_call are the same scrNativeCallContext ABI {returnValue*, argCount, argValues*}; pick one and route every native through it.
```
