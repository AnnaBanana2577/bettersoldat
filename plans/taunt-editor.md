# Plan: Taunts page in the main menu ("taunt editor")

Task: TASKS.md — add a taunt editor. Per the user: it lives in the game's menu
(the menu is what they call "the launcher"): a new rail option **Taunts** in the
SETTINGS group, **above Player and Controls** (first in the group). The page is
the editor; an Update button saves to config.cfg. The `launcher/` binary is not
touched.

What taunts are today (verified in code):
- Taunts already exist as binds in config.cfg, e.g. `bind alt+t "say_team Attack!"`.
- `client/input/input.c` resolves a held modifier to a bind name `alt+key` /
  `ctrl+key` / `shift+key`, falling back to the plain key when the combo is unbound.
  **Alt, Shift and Ctrl are all implemented already — no input change is needed.**
- `say <text>` / `say_team <text>` are console commands (client/main.c:380, the
  comment says the taunt binds use it).
- A radio call is sent today by V + two numbers: the first digit picks the call,
  the second the place, and radio_choose (client/main.c:983) sends
  `say_team "*<call><place><radio words>"` built from the `radio_*` cvars at press
  time. 3 calls × 3 places = 9 radio commands.
- `console_save` writes binds + archived cvars back into config.cfg in place,
  keeping comments — that is the "update button".
- The main menu (client/ui/mainmenu.c) already has every widget the editor needs:
  text fields (field_box), checkboxes (toggle), select boxes (select_box + popup
  list), buttons (big_button), and key capture with modifiers (bind_row,
  `capture_mod`, used by the Controls page).

## Data model: binds stay the source of truth

A taunt is a bind on one of the 36 keyboard slots (`1`–`0`, then `q`–`p`, `a`–`l`,
`z`–`m`) with a modifier prefix (**alt / shift / ctrl only**). Its text is one of
the three exclusive modes, optionally with a radio command attached on top:

| mode      | bind text                        | meaning                            |
|-----------|----------------------------------|------------------------------------|
| chat      | `say <message>`                  | said to everyone                   |
| team chat | `say_team <message>`             | said to the team                   |
| command   | anything else                    | run as console commands verbatim   |

A radio command attached to the taunt appends `; radio <call> <place>` to the bind,
so the one combo fires both — the taunt's message and the radio call, replicating
what V + two numbers do today (radio_choose reads the radio_* cvars at press time,
so renamed radio words follow). The radio attachment is independent of the three
mode checkboxes (an add-on, not a fourth mode); in practice it belongs to a team
chat taunt.

- No new cvars, no migration: the existing config.cfg taunts (alt+q … alt+h) show
  up in the editor as-is.
- The editor scans `console_bind_count`/`console_bind_at` for binds of the form
  `<mod>+<slot-key>` (mod in alt/shift/ctrl, key in the 36 slots) and classifies
  the text by prefix; a trailing `radio <c> <p>` segment is the attachment.
  Everything else on a mod+slot combo is a command taunt (documented edge: a
  non-taunt `ctrl+a` bind would appear as a command taunt).
- Message text: strip double quotes (or escape per the console's quoting rules —
  console text: `"` starts/ends a quoted word, `;` separates commands); cap at
  CONSOLE_VALUE_SIZE (256).

## Files, by repo/component (all inside the master repo)

Source of truth for the feature: **client/**. No server/, shared/, launcher/ or
input/ changes; no cross-repo dependencies. Direct changes only, in rollout order:

### 1. client/ui/taunts.c + taunts.h (new) — scan/classify/compose, headless-testable
- Pure logic over `Console*` (console/console.h only; no SDL) so tests/ can link it:
  - `taunt_slots` definition: the 36 key names, in keyboard order.
  - `taunt_find(con, slot)` → bind text or NULL; classification into
    chat / team / command; parse a trailing `radio <c> <p>` attachment back out.
  - `taunt_compose(mode, radio_index_or_0, text)` → the bind text, with quoting
    rules and the `; radio c p` suffix.
  - `taunt_set(con, mod, key, text)` → unbind the old combo on that key, bind the
    new one (empty text unbinds).
- tests/taunts_test.c (new): round-trip compose→classify for all three modes, with
  and without a radio attachment; slot set; quote stripping; unbind. Uses the
  console machinery already exercised in tests/console_test.c. Register in
  xmake.lua's `tests` target (tests/*.c is globbed already — add taunts.c to the
  tests target's files alongside the client's).

### 2. client/main.c — `radio <call> <place>` console command
- Register `radio` beside cmd_say/cmd_radio (client/main.c ≈380 / ≈971): digits
  1..3 each, running the same path the radio menu's two digits run (radio_choose):
  the first digit is the call, the second the place, sent as
  `say_team "*<call><place><radio words>"` from the radio_* cvars. This is what the
  taunt bind's `; radio c p` suffix runs, replicating V + two numbers.

### 3. client/ui/mainmenu.h + mainmenu.c — the page (the bulk)
- **mainmenu.h**: insert `MAIN_TAUNTS` into `MainPage` between `MAIN_DEMOS` and
  `MAIN_PLAYER` (`MAIN_PAGE_COUNT` stays last — the rail arithmetic relies on it).
  Add MainMenu fields for the editor state: selected slot (-1 none), edit text
  buffer, mode, radio index, modifier choice (alt/shift/ctrl).
- **mainmenu.c**:
  - `PAGE_NAMES`/`PAGE_TITLES`/`PAGE_LINES` entries (mainmenu.c:95–97); rail groups
    become PLAY `[MAIN_SERVERS, MAIN_TAUNTS)` and SETTINGS
    `[MAIN_TAUNTS, MAIN_PAGE_COUNT)` (rail(), mainmenu.c:2361) → **Taunts sits at the
    top of SETTINGS, above Player and above Controls**, as the user asked.
  - `page_taunts(Ui *ui)` — two columns (the Controls page's two-column pattern,
    mainmenu.c:1940):
    - **Left: the current ones.** A list of the taunts in slot order — key label
      (`Alt+Q` style), mode word (Chat / Team / Cmd, + "· radio" when attached),
      text preview (text_fit). Click a row → its slot loads into the editor.
      Rows reuse `row()`.
    - **Right: the keyboard.** The 36 slots drawn as key buttons: row `1`–`0`, then
      qwerty rows `q w e r t y u i o p`, `a s d f g h j k l`, `z x c v b n m`.
      Boxes via `box`/`rrect`, labels via `text_mid`, hit-test via `over`/`take`.
      A key with a taunt is tinted accent; the selected key gets the focus ring.
    - **Above the keyboard: the editor controls**, bound to the selected slot:
      1. the message text box (field_box's edit path; if it is cvar-bound, add a
         buffer-backed variant — see field_box/begin_edit at mainmenu.c:920–969);
      2. three exclusive checkboxes: **Chat / Team chat / Command** (exactly one);
      3. **Radio** select_box: None + the 9 radio calls, labelled from the radio_*
         cvars at draw time (`Enemy flagger — up!`, …); attaching one appends the
         `; radio c p` segment to the bind — the combo then says the taunt and
         sends the call;
      4. **Modifier** select_box: Alt / Shift / Ctrl (the three the input system
         already resolves; no custom modifiers);
      5. an **Update** button (big_button): `taunt_set` the bind from the controls,
         then `console_save(con, "config.cfg")` — the task's "update button that
         saves it to config.cfg". A **Clear** button (or empty text + Update)
         unbinds the slot.
  - Wire keyboard navigation for the new widgets into `mainmenu_event` (nav order),
    and give the page a working scroll (existing page scroll machinery).

### 4. config.cfg — comment the scheme (no functional change)
- Extend the comment at config.cfg:160–185: mod+key binds as taunts, the three
  modes, the `radio <call> <place>` attachment, and the Taunts page in the menu.
  Optionally add a commented example (`// bind alt+1 "say_team Base!; radio 1 2"`).
  Do not change existing lines.

### 5. readme.md — one line
- Mention the Taunts page in the menu list (the "Some keys" paragraph area).

## Rollout order & rollback

1. taunts.c/h + tests → 2. radio command → 3. menu page → 4. config.cfg comment →
5. readme. Each step leaves the build and `xmake test` green on its own; step 1
includes its new tests. Steps 1–2 are additive and can't change existing behaviour.
Rollback point per step = revert that step's changes (no commits — the human
reviews and commits per boundary).

## Validation

- `xmake build` (client, server, launcher, tests all build).
- `xmake test` — existing suite + new taunts_test.c.
- Manual, `xmake run client`:
  - Menu → Taunts page; the 14 default alt+letter taunts appear in the list and on
    the keyboard.
  - Pick Alt+Q: editor filled; change text → Update → config.cfg shows the new line
    in place, comments intact (console_save's contract).
  - Mode checkboxes exclusive; attach a radio call to a taunt (e.g. Alt+1 =
    "Enemy flagger!" + `radio 1 2`) → one combo says the message and sends the
    call, as V + 1 + 2 would; command mode (e.g. Ctrl+G = `votemap ctf_Ash`) runs
    the command.
  - Plain keys still fall back (W still jumps, Alt+W still taunts).
  - Restart: taunts persist from config.cfg; Clear removes a bind.

## Risks / notes

- Any mod+slot-key bind reads as a taunt in the editor (documented edge).
- Messages may not contain `"`; `;`-separated commands are allowed in command mode.
- A radio attachment on a chat/command taunt sends both the public line and the
  team's radio call — allowed, and the editor shows the attachment so it is never
  hidden.
- MAIN_PAGE_COUNT arithmetic: rail_count, GROUPS, PAGE_NAMES arrays must all see the
  new enum member; the page index shift is the one place a mistake breaks the rail.
- No server, shared, launcher, or input changes — if the actual `launcher/` binary
  also needs work beyond this, it is out of scope here and should be its own task.
