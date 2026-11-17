# The D2R test harness — what it gives this repository

`D2RVoidDropFix` can use the shared test harness in `C:\Users\matt7\source\repos\d2r-test-harness` to test **both** kinds of
change a mod makes, in near real time:

* **coded modifications** — inline hooks (with entry-byte fingerprints), byte
  patches, raw calls, and the telemetry those hooks emit;
* **data modifications** — table cells, stats, items, levels, skills, gold, with
  compare-and-set semantics so a test cannot silently overwrite a newer value.

The harness answers in one game frame. At the 60 Hz target a round trip is ~2
frames; the reference runtime measured p50 4 ms at 240 Hz.

## This file is generated: do not edit it

The harness maintainer generates this file with `scripts/install_agent_docs.py`,
and the same script rewrites it in every covered repository. A local edit is
overwritten on the next run and silently drifts this repository away from the
harness. If something here is wrong, missing, or does not fit your plugin,
**ask**: file a report and the maintainer changes the generator, so every
repository gets the fix at once.

```bash
cd C:\Users\matt7\source\repos\d2r-test-harness
python -m harness feedback new --kind feature --surface docs --title "docs: <what is missing>" --body "<what you ran, what the brief says, what happened>" --repo D2RVoidDropFix --tags docs
```

Use `--kind bug` when the brief contradicts the harness and `--kind feature` when
it leaves something out. Do not send a patch for `docs/harness.md`; it is not the
source -- `scripts/install_agent_docs.py` is.

## Reverse-engineering images

The game image is **`D2RLoader.exe`**: it contains `d2r.exe`, and the harness's
headless `D2R` project holds its unpacked dump (`D2RLoader_6ab3782c_dump.exe`,
stamp `0x6ab3782c`). The loader on disk is packed, so query the dump for game
addresses. `d2r_loader.dll` is **not** a harness target; do not use it for
engine addresses. `D2RCore.dll` is the other important module: the
loader-shipped engine core with its own base (`0xc0de5000000`) and the
`CoreExports` API, queried as project `D2RCore`.

## How to run it

From `C:\Users\matt7\source\repos\d2r-test-harness` (Python 3.11+; no packages to install):

```bash
python -m harness conformance --target sim     # protocol/telemetry suite on the reference server
python -m harness conformance --target host    # the same suite against the C++ core
python -m harness run scenarios/ww-hook-fires.json
python -m harness test plugins/demo-transmog.plugin.json
python -m harness validate <your-scenario-or-manifest>.json
```

`--target host` needs the C++ binary:

```bash
cmake -S cpp -B cpp/build -A x64
cmake --build cpp/build --config Release
```

That builds `cpp/build/Release/harness-core-host.exe` (the conformance host) and
`harness-core.dll` (the D2RLoader plugin). Copy the DLL to
`<slot>/d2rloader/plugins/` (loader-wide: one copy serves every mod in the slot) to serve the block from a live game.

From any repository, point the harness at a scenario file you keep next to your
code:

```bash
cd C:\Users\matt7\source\repos\d2r-test-harness
python -m harness run ../D2RVoidDropFix/tests/harness/your-scenario.json
python -m harness test ../D2RVoidDropFix/tests/harness/your-plugin.plugin.json
```

A scenario is JSON: `setup` / `steps` / `assert` / `teardown`, with symbolic
names (`stat_id: "hp"`, `rva: "whirlwind.hitDelay"`, `table_id: "itemtypes"`).
See `SCENARIOS.md` for the field reference, `VERBS.md` for the op table, and
`PLUGINS.md` for turning a mod's changes into a plugin manifest.

## Drive a live slot (slot 2 and up)

Slot 1 is the owner's game; every tool refuses it. Take a slot, start it, and
always give it back; `harness-core.dll` passes the title screen itself:

```bash
export MODKIT_RUN=my-lane
export MODKIT_HARNESS_CONFIG=C:/Users/matt7/modkit-runs/modkit-harness.toml
python ../d2rAI-toolkit-main/testing/game_slots.py --config "$MODKIT_HARNESS_CONFIG" lock 2 my-lane --wait 600
python ../d2rAI-toolkit-main/testing/game_ctl.py --config "$MODKIT_HARNESS_CONFIG" --slot 2 start
# no key step: the plugin posts Enter at TitleScreenPanel
python ../d2rAI-toolkit-main/testing/game_ctl.py --config "$MODKIT_HARNESS_CONFIG" --slot 2 status
python -m harness enter-game --slot 2      # splash + retried Play + drawn world, no script
python -m harness sweep --slot 2           # the whole round: cold start, every live scenario, one table
python -m harness live --slot 2
python ../d2rAI-toolkit-main/testing/game_ctl.py --config "$MODKIT_HARNESS_CONFIG" --slot 2 stop
python ../d2rAI-toolkit-main/testing/game_slots.py --config "$MODKIT_HARNESS_CONFIG" unlock 2 my-lane
```

`python -m harness sweep --slot 2` is the whole loop in one command: it stops
whatever was in the slot, cold-starts it, puts a character in a game, runs every
`scenarios/live-*.json` in that one session and prints one pass/fail table with
each scenario's cost and evidence directory. Exit 0 means every row passed;
`--warm` reuses a running slot and `--keep` leaves the game up afterwards.

Every command that answered something remembers it, so ask before writing a
probe: `python -m harness memory search "panel CharacterSelectPanel"` returns
what was already observed on that slot (with its evidence directory), and
`python -m harness memory list --kind trap` is the list of things that already
cost someone time. `run`, `sweep`, `test`, `enter-game`, `frontend`, `panel` and
`keys` record their answers automatically; `memory add` is for a conclusion
that no single command produced. A probe whose key already has an answer says so
before it runs (`--force` re-runs it), so a second script for the same question
is a deliberate choice rather than an accident.

A newly found RVA is reported, never typed into a plugin:
`python -m harness address propose --name unit.attackRating --rva 0x34C1A0
--confidence 0.45 --basis xref --evidence <path>`. The confidence is the chance
the *name* is right, not that the RVA is a function entry; `python -m harness
address registered` lists what the anchor table already holds, and `address list
--min-confidence 0.7` is what has earned review. `propose` reads the 16 entry
bytes at that RVA out of the unpacked image, so `address spec <id>` prints the one
line that makes it a `SPECS` entry instead of a second lookup; `address promote <id>` re-reads those bytes,
refuses if the image has moved, appends the line to `SPECS`, regenerates the
header and marks the proposal accepted (`--dry-run` writes nothing).

A memory answer records a sha256 of the slot's `harness-core.dll`, so a rebuilt
DLL makes the answer stale instead of silently current; `memory report
--older-than 30d` lists answers nobody has refreshed. A sweep stamps that hash
on every row and warns when the build changes between scenarios.

In PowerShell set `$env:MODKIT_RUN` and `$env:MODKIT_HARNESS_CONFIG` first. A cold
start reaches the menu in about 12 s, and `harness-core.dll` posts Enter itself
while `TitleScreenPanel` is up, so nothing has to press a key.

**`enter-game` is the entrance; do not write your own.** It starts or attaches,
passes the splash, retries `ui.action MainMenuHD:Play` with a short watch after
each attempt, confirms `in_game`, and waits until the world is drawn:

```bash
python -m harness enter-game --slot 2             # exit 0 = a playable character
python -m harness enter-game --slot 2 --json      # attempts, statuses, ms per step
python -m harness frontend --slot 2               # title / menu / character_list / in_game
```

Why the retry is not optional: one Play dispatched while the character list is
still opening is answered `Timeout` and does nothing, while waiting out a long
deadline for that single message is what leaves a run sitting at the character
list. And `in_game` is not the world: it flips ~3 s after Play, while the loading
screen stays up for another ~8 s, so acting on `in_game` alone acts on a loading
screen. The loader's own clock answers it: **`query.loadscreen`** (0x0117) reports
`in_progress` while a load is up and a real `duration_ms` once it ends (4-15 s on
this machine, measured, not estimated). `enter-game` reports both
(`world: true|false|null`, and `timeline`).

### Wait for the game, not for the clock, and not for a frame

Both shortcuts are wrong often enough to say plainly. A fixed `sleep` is the
first: one load took 15 s and another 4 s on the same machine, so a sleep is
always either wasted or too short. A rendered frame is the second:
`scenario.waitFrame` counts frames the renderer actually drew, and D2R throttles
drawing while its window is unfocused while ops keep being served, so a
frame-paced poll measured **1284 ms** against **120 ms** for a plain clock floor
(STATE, 2026-11-13).

Wait on state with `wait_until`; each poll is one op plus a 5 ms floor:

```json
{ "console": "goto 109" },
{ "wait_until": { "op": "query.loadscreen", "where": "in_progress", "op2": "eq", "value": 1 }, "timeout_ms": 20000 },
{ "wait_until": { "op": "query.loadscreen", "where": "finished",    "op2": "eq", "value": 1 }, "timeout_ms": 90000 },
{ "op": "query.loadscreen", "bind": "transition" }
```

The first wait is not redundant: when `goto` returns, the *previous* load's finish
stamp is still there, so waiting on `finished` alone passes instantly and times
nothing. Two waits also make a transition that never starts fail loudly instead of
silently measuring the wrong load. `mutate.goto` (0x0212) sends the same command as a verb and
answers `already_there: true, sent: false` for a transition to the current level, so that case is
named up front instead of failing the first wait.

A refused `mutate` says why: the core appends the target's own sentence to the reply's `message`
(`mutation 0x020D failed: Refused: the game refused the cast: cast.entry returned 0`). Read the
message before working around a status.

`start` takes a manual lock, `stop` leaves it, and only `unlock` frees the slot.
A command ends only the slot it started: `enter-game` leaves the character in the
game (`--stop` ends it), `run` and `test` against a slot someone else started leave
it running, and `sweep` stops the slot it cold-started. If you attach to a slot you
did not start, stop it yourself when you are done.

## From Python

```python
from harness.driver import GameCtlDriver

client = GameCtlDriver(2, config="C:/Users/matt7/modkit-runs/modkit-harness.toml").attach(timeout_s=45)
print(client.op("query.state").json())
client.ui_action("MainMenuHD", "Play")
```

`GameCtlDriver` shells out to `game_ctl.py` for process control only (`status`,
`start`, `stop`, `shot`), then opens the plugin's own named block. It never calls
`game_ctl console` or `game_ctl harness on`: those are the v1 channel of the
speedrun overlay, a different plugin with a different block.

## Console commands and debug mode

`harness.console` (0x0707) runs a line through the loader's own
`ExecuteConsoleCommand` in the plugin's process; a process with no loader console
answers `Unsupported` rather than pretending. Some commands are only accepted in
**debug mode**. Test slots 2+ launch with `-debug_mode` in the loader's
`launch_arguments`; do not send the console line `debug mode`, because that toggles
the already-on mode off. A slot started without the flag must be fixed and
restarted; the literal toggle is only a fallback:

```python
client.console("<the command that needs it>")
```

Debug mode belongs to the game process and a restart clears it.

## Driving the frontend

These verbs are the whole frontend surface. Use them instead of a script in your
repository; if one is missing, file a report rather than growing a helper.

| You want | Use | Answers |
| --- | --- | --- |
| a character in a game | `python -m harness enter-game --slot N [--character NAME]` | `in_game`, `character`, `class_id`, `attempts`, `play[]`, `world`, `timeline` |
| to know the screen | `python -m harness frontend --slot N` (`ui.frontend`, 0x070A) | `title`, `menu`, `character_list`, `in_game`, `unknown` |
| to know a panel exists | `python -m harness panel InventoryOrigin... --slot N` (`ui.panel`, 0x070B) | `found` (presence, not visibility), `rect_w`, `rect_h` |
| to send a key | `python -m harness keys i --slot N` (`ui.key`, 0x070C) | `vk`, `posted` |
| to move the cursor without clicking (an input, never an item lookup) | `python -m harness cursor 0.5 0.5 --slot N` | the screen point |
| the character rows | op `ui.characters` (0x070E) | `characters[{row, name}]`, `count`, `selected`, `mode` |
| where a row sits | op `ui.characterRects` (0x070F) | `list`, `selection` rects in frontend units |
| out of the game, cheaply | `python -m harness leave-game --slot N` (`ui.leaveGame`, 0x0710) | `left`, `frame` |
| a named character | `python -m harness enter-game --slot N --character NAME` (`ui.selectCharacter`, 0x070D) | `character`, `selected`, `class_id` |

**Hovering a cell does not identify an item, and never did.** `query.ui`'s `hovered`/`cell`/`cursor_item`
are retired fields, the probe that chased them is deleted, and there is no cell transform to fix: read
what a character carries with `query.items` (0x0102), act on a panel with a real click, and press
buttons with `ui.action` (0x0708). If a document in your repository still recommends a hover sweep or
`tools/probe_hover.py`, it is stale — delete the recommendation, do not follow it (STATE §16a).

`ui.action` (0x0708) dispatches a native UI message on the UI thread: `target` and
`command` are the same pair a layout's `onClickMessage` names. Verified live:
`ui.action("MainMenuHD", "Play")` at the menu returns `Ok` and the game enters a
town (`in_game true`, `game_ok true`). Message strings come from the game's
`ui/layouts/*.json`; other verified pairs include `FrontEndNavigation:ToLobby` and
`FrontEndNavigation:ToCharacterCreate`. A `Timeout` reply means the callback was
queued but had not run within 2 s (common mid-transition) and it can still run
afterwards: retry it, with a short watch after each attempt, which is what
`enter-game` does.

`menu` does not mean "one click from a game". D2R keeps `MainMenuPanel` resolvable
under `CharacterSelectPanel`, so a menu-only probe reports the menu while the game
waits at the character list. `ui.frontend` separates the two, and `ui.skipSplash`
now stops and says `character_list` instead of polling out its deadline.

Selecting a *named* character works and is proven for all eight classes: read
`ui.characters` fresh every time (**rows are ordered by recently played and shift
between runs**, so the name is the key and a stored row index is a bug), wait for
the list's own highlight to settle (`ui.characters` reports `selected`, two reads
150 ms apart), then press `Play` through `ui.action("MainMenuHD", "Play")`. Prove
the result with `query.player.class_id` (0 Amazon, 1 Sorceress, 2 Necromancer,
3 Paladin, 4 Barbarian, 5 Druid, 6 Assassin, 7 Warlock); an answer that names a
row is not proof that the game loaded it.

`ui.leaveGame` is the cheap way out: it dispatches the pause menu's own
`PausePanelMessage:ExitGame` (the message the `Exit` button carries in
`ui/layouts/pauselayouthd.json`) and answers `Ok` only once the game is out and the
character list is settled, so the next character can be selected in the same
process instead of paying another 26–35 s cold start. Live: a cold Warlock at 34 s,
leave in 5 s, Druid warm in 10 s.

## Evidence

A live or scenario run writes `runs/<name>-<stamp>/`: `journal.jsonl` (the last
line is the suspect when the game dies), `results.json`, `summary.txt`, telemetry
samples and screenshots. Attach that directory with `--evidence` to a feedback
report; do not hand-edit it. A missing check is `unverified`, never a pass.

**Screenshots.** A capture that is mid-load-screen, full-screen and 1.6 MB is not
something to judge an effect by, and there is no Pillow on these machines.
`python -m harness shot --slot N [--crop x,y,w,h | --crop-frac x,y,w,h] [--scale F]`
captures twice, keeps a frame that is not moving (and says `stable: false` when the
scene never stills), cuts the region you name, writes `runs/screenshots/<name>.png`
with a `.json` sidecar, and **exits 1 when the capture drew nothing** — a black or
one-colour frame is a failed capture, not evidence. `--crop` is pixels,
`--crop-frac` is 0..1 of the client area, and the unit is never guessed.
`--in FILE` crops a picture already on disk with no slot and no game. Attach the
cropped shot, not the desktop grab.

Two more files land **next to the loader** (`<slot>/`), so they survive a process
that dies before the run folder is closed:

| File | Written by | Holds |
| --- | --- | --- |
| `<slot>/d2rloader/logs/harness/harness-stall-<pid>-<ticks>.json` | the plugin's watchdog, 2 s frame stall; one a minute per process, older than a week removed | `last_opcode`, `last_dispatch_ms`, `pump_off_game_thread`, block header, thread table |
| `<slot>/d2rloader/logs/harness/harness-crash-<pid>-<ms>.json` | the client, when the process is gone; newest 20 kept per slot | `op_requested`, `published`, `in_flight`, block header, command slot, last response |

An op can die *after* its client timeout, so the client keeps a monitor thread for
the life of the connection: a death a minute after a timeout still writes the
report. `in_flight: true` means the command slot still held that op's sequence —
that, the header frame and the opcode in the same file are the "it died here"
record. Reproduce a real fault in-process with a `mem.write` that turns a known
accessor into a jump to unmapped memory (STATE §23); never test the crash path by
killing the loader, which leaves hooks installed and can hang the game instead.

## What is live vs. next

`harness-core.dll` advertises **Core, Hook, Mem, Watch**: the block, the
telemetry ring, the 64 MASM capture stubs, byte patches with expected bytes,
watch tags, session changes, and every `self.*` / `scenario.*` verb.

On the 93847 image it also advertises **Query, Mutate**, for the local player and
its stats:

* `query.state`, `query.player`, `query.unit` (the local player), `query.stat`,
  `query.stats`, `query.room`;
* `query.items` for the carried, stashed, cubed and equipped items -- id, 4-char
  code, ilvl, quality, page and grid x/y, through the loader's inventory service, so
  its data comes from the mod rather than from memory;
* `mutate.setStat`, `mutate.setLevel`, `mutate.setGold`, `scenario.snapshot`;
* `mutate.castSkill` -- the local player's own cast, through the game's cast entry (`0x218030`):
  the skill executes, spends mana, takes a cast id and stamps its cast sequence, and the reply
  carries `skill_id`, `cast_id`, `sequence` and `mana_before`. **The caster's animation is not
  driven** (a hand cast and a harness cast leave the unit in the same state, so the missing
  pose is client presentation, not game state), and `animation_driven` in the reply says so;
  mana is settled a frame later, so read `query.player` rather than expecting it in the reply;
* `mutate.goto` -- a level change through the game's own `goto <levels.txt id>` command. The
  load is asynchronous and takes seconds, so the reply is `level_before`, `sent` and
  `already_there`, never an arrival: wait on `query.loadscreen` and then read `query.room`.
  The same level starts no load at all, which `already_there: true` says up front.

A second set of verbs answers from a loader service rather than from memory, and is live on the plugin:
`query.perf` (the renderer's own frame time), `query.text` (a label by record id or key),
`query.lifecycle` (gameplay events, monster deaths, data-table readiness), `overlay.rect` /
`overlay.text` / `overlay.clear` / `overlay.list` (annotations: **visible through a temporary layered window** on
loader v1.3.1-beta, which does not composite the SDK canvas; the list is still the assertable part), and
`mem.modifications` (what bytes differ from the registry fingerprint, and whose). Inspect the labels
with `query.text` instead of copying English out of a layout JSON, and prefer `query.lifecycle` over
polling memory for "the monster died". `mem.modifications` answers an empty set on loader
v1.3.1-beta even for a verified patch, so read empty as "the loader reports nothing".

`python -m harness live --slot 2` proves the whole set in one command: verbs
against read-back, a hook on a verified anchor with a telemetry capture, and a
checked byte patch that must refuse a wrong expectation. It restores every value
it touches and needs a character already in a game.

Those were exercised in a slot-2 game. Stats 6-11 (hp, mana, stamina and their
maxima) are converted from D2's 8.8 fixed point to whole units, and experience is
unsigned 32-bit, so `query.stat` returns 138 for 138 hp and a level-99 character's
3.5 billion xp without going negative -- the same numbers the simulator gives.

At load the plugin checks the PE stamp `0x6ab3782c` and the first 16 bytes at every
anchor in its generated table. On any other image the Query/Mutate bits are not advertised
and the plugin log names the first anchor that moved; check `self.caps` before
authoring a live scenario.

The live surface is not a list that belongs in this brief: `VERBS.md` carries a marker on every row and
is the authority. Read it before assuming a verb is missing, because most of the surface is live now —
session and world reads, `mutate.goto`, `mutate.newGame`, player/stat/unit reads, items and `mutate.equip`,
gold and level, skills, tables and rules, the frontend, and the harness plane (`hook.*`, `watch.*`,
`call.*`, `mem.*`, `scenario.*`, `overlay.*`). `mutate.killUnit` is live: it resolves a type-1
`query.units` key and returns `death_pending`, with repeated calls returning `already_pending` or
`unit_gone`. What still answers `Unsupported` is small and named:
`call.raw` and the four `input.*` verbs (input replay is not exposed by the loader build);
`call.int` and `call.f64` are simulator/host surfaces only, while `call.this` is live through the
guarded fingerprint/SEH path. `mutate.spawnMonsterAt` is live, but `mspawn` cannot place a
unit, so it reports the actual landing and `position_applied: false`. `mutate.clearArea`, `mutate.setSeed` and `mutate.tableCell` <!-- lint: allow -->
are retired, with their numbers burned. The per-verb reason is in `C:\Users\matt7\source\repos\d2r-test-harness/cpp/README.md`, and the
simulator and conformance host implement every verb, so a scenario can be authored now and run against a
slot-2 game as each verb lands.

**Ask the owner what they saw.** When you prove a change on a slot, drive one
function at a time, say which function you used and what you sent, and ask the
owner to confirm the effect on screen (concrete options, and their words if it
looked different). Their answer is part of the evidence and goes in your run
record: a green reply is a reply, not a visible effect, and "the harness said
`Ok` and nothing happened on screen" is a bug worth filing. Keep the slot warm
between steps and skip the question for reads, where the reply is the evidence.

**Treat a verb as live only when its `VERBS.md` row says `live`.** The table is the
contract; a verb the table calls `sim/host only` or `not implemented` is not something
to build a live test on, whatever a note elsewhere says. The harness adds no row
without two gates — Ghidra shows the code at the address is what the row claims, and
a slot-2 game proves the verb answers it — so a verb reaches you late rather than as
a guess. If a verb you rely on refuses or changes answer, file a `--kind bug` report
with the run; do not assume your test is the broken part.

## Reporting a problem back to the harness

If the harness blocks you — a bug, an op you need, a scenario shape that cannot
express what you are testing — file it. Reports are JSON files in the harness's
`feedback/inbox/`, not chat messages:

```bash
cd C:\Users\matt7\source\repos\d2r-test-harness
python -m harness feedback new \
  --kind bug \
  --title "query.missiles returns no owner" \
  --body "Expected each missile to carry its owner id; see the attached run." \
  --repo D2RVoidDropFix \
  --severity normal \
  --tags game-verbs \
  --surface query.missiles \
  --evidence runs/<your-run-directory>
```

`--kind feature` is the right choice when the harness cannot express a test you
need. `python -m harness feedback list` shows what is open, and `show <id>`
prints one report. Include the run directory as `--evidence` whenever you have
one: it carries the journal, the telemetry samples and the response messages.

## This harness, and what is not one

The harness is `C:\Users\matt7\source\repos\d2r-test-harness`. `D2RLSpeedRunBuddy` is a speedrun overlay mod and is
**not** a test harness: its shared-memory block, its word file and its log are
not test infrastructure, and a test must not be built on top of them. `game_ctl.py`
reads its word file only as a liveness probe that a slot has a plugin loaded --
that is a probe, not proof, and it says nothing about whether a change works.
When a test needs a verb the harness does not have, file a `--kind feature`
report; do not turn another mod into a harness.

The speedrun DLL is loaded **only in its own project**. It is heavy enough to move
performance measurements and compatibility results on its own, so it is never
installed into a test slot: a slot that needs a test plugin gets `harness-core`
and nothing else.

## Ground rules

* Never run a live scenario on game slot 1 — that is the user's own game.
  `game_ctl.py` refuses it for the same reason.
* A hook needs at least 14 bytes of whole instructions as its fingerprint; the
  harness refuses an unchecked patch on purpose.
* `Refused` means the scenario is wrong; do not retry it. `Busy` means one op is
  in flight — retry after a frame.
* Artifacts land in `C:\Users\matt7\source\repos\d2r-test-harness/runs/<name>-<stamp>/`; do not hand-edit them.
