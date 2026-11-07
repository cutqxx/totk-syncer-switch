# Design Brief: TotK Save Sync, PC launcher (WPF + WPF-UI)

> Scope: the Windows launcher window (`TotkSync.exe`). The Switch text console is out of scope here. Its terminology is reused as-is.
> Source of behaviour: `docs/specs/totk-sync-lan-spec.md`. If this brief and an AC disagree on **behaviour**, the AC wins. This brief decides **how it looks and reads**.
> Mode: greenfield. Stack is fixed: .NET 10, WPF, WPF-UI (lepoco/wpfui), Fluent / Windows 11.
> Preference choices are confirmed by the developer. See "Developer decisions (2026-11-03)" at the end.

---

## 1. Design personality

**Quiet native utility.** It should look like a small tool that ships with Windows 11: Mica backdrop, system accent, Segoe UI Variable, Fluent icons. Personality comes from restraint and one confident action. The **Play** hero card is the only large, accent-filled element. Everything else is calm, readable status that tells the user "your save is safe" at a glance.

Execution pattern (Clean & Minimal, mapped to Fluent):
- No custom colours, gradients, illustrations or shadows beyond WPF-UI defaults. Depth comes from Mica + `Card` layering.
- One accent-filled control per screen (Play on Home, nothing on other pages except dialog primary buttons).
- Precise 4 epx grid, generous whitespace between groups, dense inside groups.
- Monospace only for machine values: paths, addresses, fingerprint, token, the `netsh` command.

---

## 2. Information architecture

**`ui:NavigationView` with 4 pages.** Home, Backups and Activity sit in the top group. Settings is a footer item.

Why not a single dashboard: Activity (up to 500 rows) and Backups (up to 20+ rows) would push the status cards and Play below the fold at the minimum window size. Home stays a pure "status + act" screen, which keeps it short. Lists and configuration move to their own pages (progressive disclosure).

| Page | Purpose | Contents |
|---|---|---|
| **Home** | Status at a glance + the main actions | InfoBar stack, Play hero card, PC save card, Server card (pairing) |
| **Backups** | See what's been backed up | Backups list, `Open backups folder` (Restore added in Phase 3, section 12.1) |
| **Activity** | See what the Switch did | Request log, newest on top, last 500 entries kept across restarts |
| **Settings** (footer) | Configure | Game folder, save folder override, port, backups to keep, about/data folder |

Navigation pane: `PaneDisplayMode = LeftFluent`. This is the narrow rail with an icon and a label under it, like the Windows 11 Store and Media Player. It has fixed width, no collapse logic, and labels stay visible. That's good for 4 items and for accessibility.

| Nav item | Label | Icon (`SymbolRegular`) |
|---|---|---|
| Home | `Home` | `Home24` |
| Backups | `Backups` | `Archive24` |
| Activity | `Activity` | `History24` |
| Settings (footer) | `Settings` | `Settings24` |

Window chrome: `ui:FluentWindow` with `ExtendsContentIntoTitleBar = true`, `WindowBackdropType = Mica`, `ui:TitleBar` with title `TotK Save Sync` and the app icon. Single window, no tray, no secondary windows. Dialogs are `ui:ContentDialog` hosted in the main window (via `ContentDialogService` / a `ContentPresenter` host). Transient confirmations use `ui:Snackbar` via `SnackbarPresenter`.

---

## 3. Window, layout grid, responsiveness

All sizes are WPF device-independent pixels (epx). They scale with DPI automatically.

| Property | Value |
|---|---|
| Default size | 1100 × 720 |
| `MinWidth` × `MinHeight` | 760 × 540 (fits 1366×768 at 125% scale with taskbar) |
| Startup location | `CenterScreen`. Size/position persistence: not in scope (YAGNI) |
| Page content | inside a vertical `ScrollViewer` on every page (never clip at min size) |
| Content max width | 1080, left-aligned within the page (Windows Settings style). Prevents line stretching on wide monitors |
| Page padding | 32 left/right, 24 top, 32 bottom |

**Home grid (≥ 1000 epx content width):**
- Row 1: InfoBar stack (full width, collapses to 0 height when empty).
- Row 2: Play hero card (full width).
- Row 3: two columns, `3*` PC save card | `2*` Server card, 16 gutter. Asymmetric on purpose: the save card carries more text.

**Home grid (< 1000 epx):** single column, order InfoBars → Play → PC save → Server. Implement with a width trigger / adaptive layout, not two separate views.

Tab order = visual order = reading order on both layouts.

---

## 4. Visual system

### 4.1 Spacing scale (4 epx base)

`4 / 8 / 12 / 16 / 24 / 32 / 48`. Do not use other values.

| Use | Value |
|---|---|
| Icon ↔ its text label | 8 |
| Label ↔ value inside a field row | 4 |
| Between rows inside a card | 12 |
| Card inner padding | 16 (hero card: 24) |
| Between cards / InfoBars | 16 |
| Between page title and first content | 24 |
| Between setting groups (section header above) | 32 above, 8 below header |

Define these as `Thickness`/`Double` resources in one app resource dictionary (e.g. `Space.S = 8`, `Space.M = 16`). Never inline magic numbers.

### 4.2 Typography

Fluent type ramp via WPF-UI `ui:TextBlock FontTypography`. Fonts: **Segoe UI Variable** (system UI font, default for WPF-UI, falls back to Segoe UI on Windows 10). This is the deliberate exception to the "avoid system fonts" rule: the whole design intent is "native Windows 11". The second family is monospace for machine values.

| Role | FontTypography | Size / line height | Weight | Used for |
|---|---|---|---|---|
| Page title | `Title` | 28 / 36 | Semibold | "Home", "Backups"… |
| Hero title | `Subtitle` | 20 / 28 | Semibold | "Tears of the Kingdom" in Play card |
| Card title / section header | `BodyStrong` | 14 / 20 | Semibold | "PC save", "Server", "Network" |
| Status line | `BodyStrong` | 14 / 20 | Semibold | "In sync", "Running" |
| Body / values | `Body` | 14 / 20 | Regular | most text |
| Field labels, secondary info | `Caption` | 12 / 16 | Regular | "Modified", "Fingerprint", hints |
| Machine values | Body size, `Cascadia Mono, Consolas` | 14 / 20 (13 in tables) | Regular | paths, `http://…`, fingerprint, token, `netsh` |

Colour of text: `TextFillColorPrimaryBrush` for values and status words, `TextFillColorSecondaryBrush` for labels and hints. Never use `TextFillColorTertiaryBrush` or disabled brushes for information the user needs (see 9.3).

Long paths: `TextTrimming = CharacterEllipsis` + `ToolTip` with the full path. The full path must also be reachable by keyboard: the `Open` button next to it, plus `AutomationProperties.HelpText` = full path.

### 4.3 Colour

**Accent: system accent colour.** No custom palette and no hex values in XAML. Everything comes from WPF-UI theme resources, so light/dark/high-contrast follow Windows.

Theme: follow Windows via WPF-UI `SystemThemeWatcher` on the main window (with accent updates). No theme setting in the app (settings.json has none; YAGNI).

| Purpose | Theme resource (WPF-UI) |
|---|---|
| Window background | Mica (fallback: `ApplicationBackgroundBrush`) |
| Cards | `ui:Card` / `ui:CardControl` defaults (`CardBackgroundFillColorDefaultBrush`, `CardStrokeColorDefaultBrush`) |
| Primary text / secondary text | `TextFillColorPrimaryBrush` / `TextFillColorSecondaryBrush` |
| Accent (Play, primary dialog buttons, links) | `ui:Button Appearance="Primary"`, `AccentTextFillColorPrimaryBrush` |
| Success icon | `SystemFillColorSuccessBrush` |
| Caution icon | `SystemFillColorCautionBrush` |
| Critical icon / inline validation text | `SystemFillColorCriticalBrush` |
| Neutral/info icon | `SystemFillColorAttentionBrush` (info) or `TextFillColorSecondaryBrush` (neutral) |
| Monospace value blocks (token, command) | `ControlFillColorDefaultBrush` background + `ControlStrokeColorDefaultBrush` 1 epx border, corner radius 4 |

60-30-10: Mica/background ≈ 60%, card surfaces ≈ 30%, accent ≈ 10% (Play button, focused/selected nav item, dialog primary buttons, hyperlinks).

If a resource key above is missing in the installed WPF-UI version, use the nearest key from its theme dictionary. **Never substitute a hex value.**

### 4.4 Shape and elevation

Use WPF-UI defaults: control corner radius 4, card/overlay radius 8, 1 epx strokes, no custom drop shadows. Don't restyle `Card` or `Button` templates.

### 4.5 Iconography

Fluent System Icons via `ui:SymbolIcon Symbol="…"`. Size 24 in nav and the hero card, 20 in card headers/status lines, 16 inline (next to validation text, in table cells). Decorative icons next to a text label: hide from automation (`AutomationProperties.AccessibilityView="Raw"` or no name). Icon-only buttons must have `AutomationProperties.Name` + `ToolTip`.

Verify every symbol name in the WPF-UI Gallery for the installed version. If a name is missing, pick the closest from the same family. Don't use emoji or bitmaps.

| Meaning | `SymbolRegular` |
|---|---|
| Play | `Play24` |
| Game running | `Games24` |
| PC save | `Save24` |
| Server | `Server24` |
| In sync / OK / Running | `CheckmarkCircle24` (16/20 variants as sized) |
| PC has new progress | `ArrowSync24` |
| Never synced / info | `Info24` |
| Warning (port, firewall, conflict, save not found) | `Warning24` |
| Error | `ErrorCircle24` |
| Transfer in progress | `ui:ProgressRing` (indeterminate, 16–20) instead of an icon |
| Firewall | `Shield24` |
| Copy | `Copy24` |
| Show / Hide token | `Eye24` / `EyeOff24` |
| Open folder | `FolderOpen24` |
| Choose folder / Browse | `Folder24` |
| Save switch-config.json | `ArrowDownload24` |
| Received from Switch | `ArrowDownload24` |
| Sent to Switch | `ArrowUpload24` |
| Restore (Phase 3 only) | `ArrowCounterclockwise24` |
| Retry | `ArrowClockwise24` |

### 4.6 Motion

WPF-UI defaults only: page transition, InfoBar open, ProgressRing. No custom animations. If Windows "Animation effects" is off (`SystemParameters.ClientAreaAnimation == false`), set the NavigationView transition to none. ProgressRing stays (it's a status, not decoration).

---

## 5. Status model (single source for every status visual)

Every status = **icon + status word + detail text**. Colour is only on the icon. The status word is always in `TextFillColorPrimaryBrush`. Status lines get `AutomationProperties.LiveSetting="Polite"` so screen readers announce changes.

### 5.1 PC save card states

| State | Icon | Status word | Detail line(s) |
|---|---|---|---|
| In sync | `CheckmarkCircle20` success | `In sync` | `Last sync: 2026-11-03 14:25, Switch -> PC` |
| PC has new progress | `ArrowSync20` attention | `PC has new progress` | `Last sync: 2026-11-03 12:00, Switch -> PC` / `Use "Get from PC" on Switch to continue there.` |
| Never synced | `Info20` neutral | `Never synced` | `Use "Send to PC" or "Get from PC" on Switch.` |
| Receiving | ProgressRing | `Receiving save from Switch…` | `Don't start the game until this finishes.` |
| Sending (Phase 2) | ProgressRing | `Sending save to Switch…` | (none) |
| Not found | `Warning20` caution | `PC save not found` | `PC save not found: start TotK on PC once or set save folder in Settings` + button `Open Settings` |
| Several candidates | `Warning20` caution | `Several save folders found` | `Choose which one TotK Save Sync should use.` + list (see 6.3) |
| No game folder | (card replaced by setup card, see 7) | | |

Fields (when a save is found), as label (Caption, secondary) above value (Body, primary):

| Label | Value example | Notes |
|---|---|---|
| `Folder` | `D:\Games\totk-pc-recomp\profile\0100…` (mono, trimmed) | + icon button `Open folder` (`FolderOpen24`) |
| `Modified` | `2026-11-03 14:25` | local time, format `yyyy-MM-dd HH:mm` (same as Switch) |
| `Files` | `27 files, 19.2 MB` | same size formatter everywhere |
| `Fingerprint` | `3f9a1c0b7e21` (mono) | first 12 hex chars after `sha256:`; tooltip = full value |

The "Last sync" text keeps the spec's ASCII arrow `Switch -> PC` / `PC -> Switch`, so it matches the AC strings and the Switch console.

### 5.2 Server card states

| State | Icon | Status word | Detail |
|---|---|---|---|
| Starting | ProgressRing | `Starting…` | |
| Running | `CheckmarkCircle20` success | `Running` | `Listening on port 8765` |
| Running, no LAN address | `Warning20` caution | `Running` | `No home network address found. Connect this PC to the same network as the Switch.` |
| Error | `ErrorCircle20` critical | `Error` | `Error: port 8765 is busy (is the launcher already running?)` + `Retry` + `Change port` |
| Stopping (window closing) | ProgressRing | `Stopping…` | |

### 5.3 Play button states

The Play hero card shows the button plus a **state caption under it in secondary (not disabled) text**, so the reason is always readable.

| State | Button | Enabled | Caption under button |
|---|---|---|---|
| Ready | `Play24` + `Play` | yes | (none) |
| Ready, PC save not found | `Play` | yes | `Starting the game once creates the PC save.` |
| Starting | ProgressRing + `Starting…` | no | |
| Game running | `Games24` + `TotK is running` | no | `Sync is paused until you close the game.` |
| Waiting for transfer | ProgressRing + `Waiting…` | no | `Waiting for save transfer to finish. The game starts automatically.` |
| No game folder | `Play` | no | `Choose the game folder first.` (only outside first-run setup) |
| Launch failed | back to Ready | yes | + Error InfoBar (see 8) |

Size: `MinHeight 56`, `MinWidth 200`, `Appearance="Primary"`, icon 24, text `BodyStrong`. It's the only Primary button on Home. All other Home buttons use `Appearance="Secondary"`, and icon-only buttons use `Transparent`.

---

## 6. Home page

### 6.1 Wireframe (wide)

```
+----------------------------------------------------------------------------------+
| [app] TotK Save Sync                                               _   []   X    |  <- FluentWindow, Mica
+-------+--------------------------------------------------------------------------+
| [⌂]   |  Home                                                                    |
| Home  |                                                                          |
|       |  +--------------------------------------------------------------------+  |
| [▣]   |  | (✓) Save received from Switch                                    X |  |  <- InfoBar stack
|Backups|  |     PC save replaced. Previous save backed up as                   |  |
|       |  |     pc-20261103-142501-before-switch.zip      [Open backups folder]|  |
| [↺]   |  +--------------------------------------------------------------------+  |
|Activity| +--------------------------------------------------------------------+  |
|       |  |  Tears of the Kingdom                         +------------------+ |  |  <- hero card (24 pad)
|       |  |  D:\Games\totk-pc-recomp                      |  |>   Play      | |  |
|       |  |                                               +------------------+ |  |
|       |  +--------------------------------------------------------------------+  |
|       |  +---------------------------------------+ +--------------------------+  |
|       |  | [save] PC save                        | | [srv] Server             |  |
|       |  | (✓) In sync                           | | (✓) Running              |  |
|       |  | Last sync: 2026-11-03 14:25,          | | Listening on port 8765   |  |
|       |  |   Switch -> PC                        | |                          |  |
|       |  |                                       | | Address for Switch       |  |
|       |  | Folder                                | | [http://192.168.1.10:8765 v] [⧉]|
|       |  | D:\Games\totk-pc-recomp\prof… [open]  | |                          |  |
|       |  | Modified          Files               | | Token                    |  |
|       |  | 2026-11-03 14:25  27 files, 19.2 MB   | | [••••••••••••••••] [Show] [Copy]|
|       |  | Fingerprint                           | |                          |  |
|       |  | 3f9a1c0b7e21                          | | [⇩ Save switch-config.json…]|
|       |  |                                       | | [🛡 Firewall help]        |  |
|       |  |                                       | | Last request: 14:25:03   |  |
|       |  |                                       | |   from 192.168.1.23      |  |
|       |  +---------------------------------------+ +--------------------------+  |
| [⚙]   |                                                                          |
|Settings|                                                                         |
+-------+--------------------------------------------------------------------------+
```
(Glyphs in the wireframe stand in for Fluent icons. Don't use them as text.)

### 6.2 Hero (Play) card
- `ui:Card`, padding 24. Left: `Tears of the Kingdom` (Subtitle) + game folder path (Caption, mono, trimmed). Right: Play button + state caption (5.3).
- Initial keyboard focus on window open: the Play button when enabled, otherwise the first actionable control.

### 6.3 PC save card
- `ui:Card` with header row (icon `Save20` + `PC save`), then the status line (5.1), then a 2-column field grid (Folder spans both columns).
- **Several candidates:** replace the field grid with a list. Each row: path (mono) + `Modified 2026-11-03 14:25` (Caption) + Secondary button `Use this folder`. Clicking it writes `save_dir` (the user's explicit choice; the launcher never auto-picks). Under the list: hyperlink `Open Settings`.
- **Receiving:** the card shows the state from 5.1. Fields keep showing the *old* values until the replace completes, then update together (matches the "old or new, never intermediate" AC).

### 6.4 Server card
- `ui:Card` header `Server20` + `Server`, status line (5.2).
- `Address for Switch`: if exactly one private IPv4, show a mono TextBlock + icon button Copy. If several, show a `ComboBox` listing all `http://ip:port` values. The selection is what `switch-config.json` uses (AC 1.3).
- `Token`: a mono value block showing **16 fixed bullets** `••••••••••••••••` (fixed count, so the length doesn't leak). Buttons `Show` (`Eye24`) ↔ `Hide` (`EyeOff24`), and `Copy` (`Copy24`). Copy doesn't reveal the token. The token is hidden again on every app start. It's never in tooltips, automation names, Activity (on screen or in its file) or logs. When revealed, its automation name is `Token` (the value is readable only as the TextBlock text).
- `Save switch-config.json…` (Secondary, `ArrowDownload24`) opens a Save dialog with default file name `switch-config.json`. On success: Success InfoBar (8).
- `Firewall help` (Secondary, `Shield24`) opens the firewall dialog (10).
- Footer caption, **based on the current launcher session only** (not the persisted Activity log, so it reflects whether the Switch can reach the server right now): `No requests from the Switch yet` or `Last request: 14:25:03 from 192.168.1.23`.

---

## 7. First-run flow

"First run" means `settings.json` didn't exist when the app started. "Not configured" means no valid `game_dir`.

1. **Window opens. The server starts and the token is generated** (per spec). Home shows a **Setup card in place of the Play hero + PC save card**. The Server card stays visible on the right (pairing works before the game folder is chosen).
2. Setup card (`ui:Card`, padding 24):
   - Title (Subtitle): `Set up TotK Save Sync`
   - Step 1 header (BodyStrong): `1. Choose your TotK PC folder`
   - Body: `Pick the folder that contains totk.exe.`
   - Button (Primary, the only Primary on screen in this state): `Choose folder…` (`Folder24`) opens the folder picker (`Microsoft.Win32.OpenFolderDialog`, .NET 8+).
   - Validation error (inline, `ErrorCircle16` + critical text, under the button): `totk.exe not found in D:\Downloads\something`. The setting is not saved and focus returns to the button.
   - After the folder is accepted, show the result in the same card:
     - Found: `(✓) PC save found` + path (mono).
     - Not found: `(!) PC save not found: start TotK on PC once or set save folder in Settings`. This isn't a blocker, and Play becomes available.
     - Several: candidate list with `Use this folder` (as in 6.3).
   - Step 2 header: `2. Connect your Switch`
   - Body: `Save switch-config.json and copy it to the SD card as sd:/config/totk-sync/config.json. Then use "Send to PC" on the Switch.` Points to the Server card buttons. No duplicate buttons.
   - Button `Done` (Secondary) switches Home to the normal layout. Also switch automatically on next start once `game_dir` is valid.
3. **Firewall dialog opens automatically once, right after the game folder is accepted on first run.** It doesn't open over the folder picker. That satisfies "on first run" without stacking two modal things at launch.
4. While not configured: Play is disabled (`Choose the game folder first.`). Backups and Activity remain viewable (read-only, they show empty or loaded states). Settings shows the Game folder row with `Browse…`.

Wireframe (not configured):
```
|  Home                                                                      |
|  +---------------------------------------+ +--------------------------+    |
|  | Set up TotK Save Sync                 | | [srv] Server             |    |
|  |                                       | | (✓) Running              |    |
|  | 1. Choose your TotK PC folder         | | Address for Switch ...   |    |
|  | Pick the folder that contains         | | Token ••••• [Show][Copy] |    |
|  | totk.exe.                             | | [Save switch-config.json…]|   |
|  | [📁 Choose folder…]                   | | [Firewall help]          |    |
|  | (x) totk.exe not found in D:\Down…    | +--------------------------+    |
|  |                                       |                                 |
|  | 2. Connect your Switch                |                                 |
|  | Save switch-config.json and copy it   |                                 |
|  | to the SD card as                     |                                 |
|  | sd:/config/totk-sync/config.json …    |                                 |
|  |                              [Done]   |                                 |
|  +---------------------------------------+                                 |
```

---

## 8. Banners (InfoBar) on Home

Rules:
- `ui:InfoBar` stack at the top of Home, 16 apart. **One InfoBar per kind**: a new event of the same kind replaces the previous one. Order: Error → Warning → Informational → Success. Max 3 visible; older closable ones drop off first.
- Each InfoBar: `Severity` icon + **Title (status word) + Message (what happened, what's safe, what to do)** + at most one action button. Colour is never the only signal.
- Closable unless noted. Condition banners (game running, port busy) close themselves when the condition ends.
- Success/rejection events raised while the user is on another page still appear when they come back to Home. They are also always written to Activity.

| Kind | Severity | Closable | Title | Message | Action |
|---|---|---|---|---|---|
| Save received | Success | yes | `Save received from Switch` | `PC save replaced. Previous save backed up as pc-20261103-142501-before-switch.zip.` | `Open backups folder` |
| Same save received | Informational | yes | `PC already has this save` | `The Switch sent the same save. Nothing was changed.` | |
| Conflict (push rejected) | Warning | yes | `Switch tried to send a save` | `The PC save has new progress since the last sync (PC: 2026-11-03 14:25, last sync: never). Nothing was changed. To overwrite it, choose "A - yes" on the Switch. A backup is made first.` | |
| Invalid ZIP | Error | yes | `Switch save rejected` | `missing option.sav. Nothing was changed.` (reason from spec rule) | `View activity` |
| Replace failed | Error | yes | `Couldn't replace the PC save` | `<reason>. Your PC save was not changed.` | `View activity` |
| Port busy | Error | no (while condition) | `Server can't start` | `Error: port 8765 is busy (is the launcher already running?)` | `Change port` (opens Settings, focuses Port) |
| Firewall may block (Phase 3.3) | Warning | yes | `No requests from the Switch yet` | `Windows Firewall may be blocking the connection. Check that this network is Private.` | `Firewall help` |
| Game running | Informational | no (while running) | `TotK is running` | `Sync is paused until you close the game. The Switch can't send or get saves meanwhile.` | |
| New PC progress (after game exit) | Informational | yes | `PC save has new progress` | `PC save has new progress: use "Get from PC" on Switch` | |
| Save sent to Switch (Phase 2) | Informational | yes | `Save sent to Switch` | `Waiting for the Switch to confirm.` | |
| Sync confirmed (Phase 2) | Success | yes | `Sync confirmed by Switch` | `Last sync: 2026-11-03 14:25, PC -> Switch` | |
| Launch failed | Error | yes | `Couldn't start totk.exe` | `<reason>` | |
| switch-config saved | Success | yes | `switch-config.json saved` | `Copy it to the SD card as sd:/config/totk-sync/config.json. It contains your token, so don't share it.` | `Open folder` |
| Port changed | Informational | yes | `Port changed to 9000` | `Restart TotK Save Sync to use it, then save switch-config.json again.` | |

InfoBar Title/Message get `AutomationProperties.LiveSetting`: `Assertive` for Error, `Polite` for others.

Snackbar (3 s, bottom, `Polite`) for copy confirmations only: `Token copied`, `Address copied`, `Command copied`.

---

## 9. Dangerous actions, transfers and threading

The launcher never asks the user to confirm an incoming push. The Switch owns that choice (A/B), so the PC's job is **clear, calm reporting**.

### 9.1 Copy principles
- Every message says: **what happened → what's safe → what to do next**. Example: `…Nothing was changed.` / `…Previous save backed up as <name>.`
- Always name the backup file when a save was replaced.
- Never say "fatal", "corrupt", "danger", or use exclamation marks or ALL CAPS (caps are the Switch console's style, not the launcher's).
- Use the Switch action names verbatim with quotes: `"Send to PC"`, `"Get from PC"`, `"Backup to SD"`, `"A - yes"`.
- Sentence case for every label, button and title (Fluent). Buttons that open a dialog end with `…` (U+2026).
- Dates `yyyy-MM-dd HH:mm` (log times `yyyy-MM-dd HH:mm:ss`), local time, same as the Switch.

### 9.2 Transfers
- Incoming push: PC save card enters `Receiving save from Switch…`. Play goes to `Waiting…` if pressed. On completion, the card updates in one step and the Success/Info InfoBar appears.
- **Window close during a write:** closing is deferred. Show a `ui:ContentDialog` with no buttons: Title `Finishing save transfer`, body `TotK Save Sync will close when the save is fully written. This takes a few seconds.` + ProgressRing. It closes itself and then the window closes. No Cancel (cancelling mid-transfer is out of scope).

### 9.3 Threading and feedback (non-negotiable)
- No file, ZIP, hash, process, network or activity-log file work on the UI thread. The UI only reacts to state changes.
- Any button that starts async work disables itself and shows a ProgressRing or label change until it finishes. Never freeze; never show a modal spinner for under-a-second work.
- Disabled controls use WPF-UI disabled styling (low contrast is allowed for inactive controls under WCAG). **The reason they're disabled is always shown in readable secondary text nearby** (e.g. the Play caption), never only in the disabled control.

---

## 10. Firewall help dialog

`ui:ContentDialog`, max width 560, Close button only (`CloseButtonText = "Close"`, default).

```
+--------------------------------------------------------------+
| Allow the Switch to connect                                  |
|                                                              |
| Windows Firewall blocks incoming connections on Public       |
| networks. TotK Save Sync only needs your home (Private)      |
| network.                                                     |
|                                                              |
| 1. If Windows asks about TotK Save Sync, allow it on         |
|    Private networks.                                         |
| 2. Make sure this PC's network is set to Private.            |
|    [Open network settings]                                   |
| 3. If the Switch still can't connect, run this command in    |
|    a terminal opened as administrator:                       |
|    +------------------------------------------------------+  |
|    | netsh advfirewall firewall add rule name="TotK Save  |  |
|    | Sync" dir=in action=allow protocol=TCP               |  |
|    | localport=8765 profile=private                       |  |
|    +------------------------------------------------------+  |
|                                                  [⧉ Copy]   |
|                                                              |
| To check: on a phone on the same Wi-Fi, open                 |
| http://192.168.1.10:8765/status. "Unauthorized" means the    |
| connection works.                                            |
|                                                     [Close]  |
+--------------------------------------------------------------+
```
- Command block: read-only, selectable `TextBox` (mono, wrap, `ControlFillColorDefaultBrush`), so it's keyboard-focusable and copyable. The port comes from settings.
- `Open network settings`: hyperlink button opening `ms-settings:network-status`. It opens Windows Settings and doesn't change anything.
- **No "Run as administrator" button.** The spec lists "adding the firewall rule by the launcher" as out of scope, so the launcher only explains and provides the command plus Copy.
- If the architecture picks `HttpListener` and a `netsh http add urlacl` reservation is needed, add it as a second command block with its own Copy, using the same pattern.

---

## 11. Activity page

**Persistence:** the log survives restarts. The **last 500 entries** are kept in a file under `%LOCALAPPDATA%\TotkSync\`; the architecture chooses the file name and format. When entry 501 is added, the oldest is dropped. The file never contains the token, the `Authorization` header or any request body, only the fields shown in the table below. The page shows exactly what the file holds plus new entries from this session.

- Title `Activity`, subtitle (Caption): `Requests from the Switch, newest first. The last 500 are kept.`
- `ListView` + `GridView` (or `ui:DataGrid` read-only) with columns:

| Column | Example | Notes |
|---|---|---|
| `Time` | `2026-11-03 14:25:03` | local time, full date because entries span days |
| `Client` | `192.168.1.23` | mono |
| `Request` | `PUT /save` | mono; method + path, **never headers or query values other than `force`** |
| `Result` | `(✓) Save received` / secondary `200` | icon 16 + friendly text + HTTP code in secondary text |

Result text mapping (icon in parentheses):

| Outcome | Result text |
|---|---|
| status ok | (✓) `OK` |
| PUT replaced | (✓) `Save received, backup pc-…zip` |
| PUT unchanged | (✓) `Same save, nothing changed` |
| GET /save ok | (↑) `Save sent to Switch` |
| ack recorded | (✓) `Sync confirmed by Switch` |
| 401 | (x) `Rejected: wrong or missing token` |
| 403 | (x) `Rejected: not a private network address` |
| 409 conflict | (!) `Rejected: PC save has new progress` |
| 409 fingerprint_mismatch | (!) `Rejected: save changed since it was sent` |
| 400 invalid_zip | (x) `Rejected: <reason>` |
| 404 no_save | (!) `Rejected: no PC save yet` |
| 404/405 unknown | (x) `Not found` / `Method not allowed` |
| 411 / 413 | (x) `Rejected: missing size` / `Rejected: save too large` |
| 423 | (!) `Rejected: TotK is running` |
| 503 | (!) `Rejected: busy with another transfer` |

**Page states:**

| State | What the user sees |
|---|---|
| Loading (file read at startup, async) | ProgressRing 20 + `Loading activity…` in the list area. Usually too fast to notice, so show it only if loading takes over ~300 ms |
| Loaded | the list, newest first. New entries are inserted at the top without moving keyboard focus or the scroll position when the user has scrolled down |
| Empty (no file, or file with 0 entries) | `History24` 48 secondary + title `No requests yet` + body `When the Switch connects, its requests show up here. This window must stay open.` |
| File unreadable | a Warning `InfoBar` at the top of the page (not closable while shown on this page): title `Couldn't read the earlier activity`, message `New requests are still recorded. Earlier entries may be missing.` The list then behaves like Empty/Loaded with this session's entries. Never block startup or the server on this |

- Rows are read-only. Keyboard: list is focusable, arrow keys move between rows, each row's automation name = `"2026-11-03 14:25:03, 192.168.1.23, PUT /save, Save received"`.
- The token never appears anywhere on this page, in tooltips, in copied text or in the log file.

**"Clear log": not added (recommendation).** The 500-entry cap already bounds the file, and the log holds nothing sensitive (no token, only IPs, paths and results). Clearing it would also remove the trail the developer uses to debug Switch connections during the [HW] checks. If a reset is ever needed, the file can be deleted while the launcher is closed; the About row's `Open data folder` makes that easy. Add a `Clear log` button only if a real need comes up (YAGNI).

---

## 12. Backups page (Phases 1–2)

- Title `Backups`. Header row: caption `%LOCALAPPDATA%\TotkSync\backups` (mono, trimmed) + Secondary button `Open backups folder` (`FolderOpen24`).
- List columns: `Date` (`2026-11-03 14:25`), `Reason`, `Size` (`18.7 MB`), `File` (mono, secondary, trimmed). Newest first. Rows are read-only; selection is visual only in Phases 1–2.
- Reason labels: `pc-…-before-switch.zip` → `Before save from Switch`.
- Footer caption: `Keeping the 20 newest backups. Change this in Settings.` (value from settings).
- Empty state (`Archive24` 48): title `No backups yet`, body `A backup of your PC save is made automatically before the Switch replaces it.`
- **No Restore control, disabled button or "later version" text in Phases 1–2.** The page is only the list + `Open backups folder`.

### 12.1 Phase 3 only: Restore from backups (Slice 3.2)

Not built before Phase 3. Specified here so Phase 3 doesn't need a new design pass.
- Add a Secondary button `Restore…` (`ArrowCounterclockwise24`) to the header row, after `Open backups folder`. Row selection becomes meaningful.
- Enabled when a row is selected and TotK isn't running. When it's disabled, a caption next to it says why: `Select a backup to restore.` / `Close TotK to restore a backup.`
- Confirmation `ui:ContentDialog`: Title `Restore this backup?` Body: `Your PC save will be replaced with the backup from 2026-11-03 14:25. The current save is backed up first, so you can undo this.` `PrimaryButtonText = "Restore"`, `CloseButtonText = "Cancel"`, **`DefaultButton = Close`** (Enter doesn't restore by accident).
- While restoring: the button shows ProgressRing + `Restoring…`, the PC save card shows a matching in-progress state, and the UI stays responsive.
- Success InfoBar (on Backups and Home): `Backup restored` / `Current save backed up as <name>.` Failure: Error InfoBar `Couldn't restore the backup` / `<reason>. Your PC save was not changed.`
- Backups created by a restore get their own reason label from the domain (e.g. `Before restore`).
- Activity gets an entry for the restore (local action, Client shown as `This PC`).

---

## 13. Settings page

Windows 11 Settings pattern: section headers (BodyStrong) + `ui:CardControl` rows (icon, header, description, control on the right). Values apply **on commit** (folder picked, or Enter / focus leaves a NumberBox). There's no global Save button. Invalid values show an inline error under the control (`ErrorCircle16` + critical text, `LiveSetting=Assertive`) and are **not saved**. The field keeps the invalid text until the user fixes it.

| Section | Row header | Description | Control | Error text |
|---|---|---|---|---|
| Game | `Game folder` | `Folder that contains totk.exe.` | path (mono, trimmed) + `Browse…` | `totk.exe not found in <path>` |
| Game | `Save folder` | `Found automatically in <game folder>\profile. Set it only if the wrong folder is detected.` | `Automatic: <path>` or `Custom: <path>` + `Browse…` + `Use automatic` (only when custom) | (state shown on Home card) |
| Network | `Port` | `The Switch connects to this port. Default: 8765.` | `ui:NumberBox` 1024–65535, no spin-wrap | `Port must be between 1024 and 65535.` |
| Backups | `Backups to keep` | `Older launcher backups are deleted automatically.` | `ui:NumberBox` min 1 | `Keep at least 1 backup.` |
| About | `TotK Save Sync <version>` | `Data folder: %LOCALAPPDATA%\TotkSync (settings, activity log, backups)` · `Follows your Windows light or dark theme.` | `Open data folder` | |

- A committed port change shows the `Port changed` InfoBar (8). The new port takes effect after the launcher restarts; the running server is not restarted live.
  - Слайс 1.3: вместо перезапуска лаунчера сервер перезапускается сам; напоминание пересохранить switch-config.json показывается InfoBar.
- The Port and Backups NumberBoxes have `AutomationProperties.Name` = row header, and the error text is linked via `AutomationProperties.HelpText`.
- The "Change port" action from the port-busy banner navigates here and focuses Port.

---

## 14. Accessibility (priority 1)

- **Keyboard:** every action reachable by Tab/Shift+Tab, activated by Enter/Space. NavigationView items reachable and switchable with arrow keys. Tab order follows the visual order (sections 6.1, 7). Esc closes dialogs. Access keys (underscore) on the main buttons: `_Play`, `Show`/`_Copy` token, `_Save switch-config.json…`, `_Firewall help`.
- **Focus:** keep WPF-UI's Fluent focus visual (2 epx outer stroke). Never set `FocusVisualStyle="{x:Null}"` without a replacement. Focus must be visible on Mica in both themes. After a dialog closes, focus returns to the button that opened it. New Activity entries never steal focus.
- **Status not by colour alone:** every status = icon shape + word (section 5). Success/Caution/Critical icons differ in shape (check / triangle / circle-x).
- **Contrast:** only theme brushes (section 4.3), which meet WCAG AA in light and dark. Required text is never in tertiary or disabled brushes. Test with Windows contrast themes (WPF-UI high-contrast resources): status words and icons must stay visible.
- **Targets:** WPF-UI default buttons (32 epx) meet WCAG 2.2 AA target size (24×24). Keep icon-only buttons at least 32×32, the Play button 56 high. Don't shrink controls below WPF-UI defaults.
- **Screen readers:** `AutomationProperties.Name` on every icon-only button (`Copy address`, `Copy token`, `Show token`, `Open save folder`) and on value TextBlocks (`PC save folder`, `Fingerprint`). Status lines and InfoBars are live regions (sections 5, 8). The token's automation name never contains its value while hidden.
- **DPI:** per-monitor v2 DPI awareness in the app manifest. Vector icons only. App icon `.ico` with 16/24/32/48/256 sizes. Test at 100%, 150%, 200%, and on a monitor change.
- **Min size:** 760 × 540 with vertical scrolling. Nothing clipped or overlapping at 200% text scaling (Windows "Text size" also scales WPF-UI text): rows wrap, they don't truncate status words.
- **Reduced motion:** see 4.6.

---

## 15. Microcopy reference (exact strings)

| Where | String |
|---|---|
| Window title | `TotK Save Sync` |
| Nav | `Home`, `Backups`, `Activity`, `Settings` |
| Hero title | `Tears of the Kingdom` |
| Play | `Play` / `Starting…` / `TotK is running` / `Waiting…` |
| Play captions | `Sync is paused until you close the game.` / `Waiting for save transfer to finish. The game starts automatically.` / `Choose the game folder first.` / `Starting the game once creates the PC save.` |
| PC save card | `PC save`, `In sync`, `PC has new progress`, `Never synced`, `Receiving save from Switch…`, `Sending save to Switch…`, `PC save not found`, `Several save folders found`, `Use this folder`, `Open Settings` |
| Field labels | `Folder`, `Modified`, `Files`, `Fingerprint`, `Address for Switch`, `Token` |
| Sync detail | `Last sync: <yyyy-MM-dd HH:mm>, Switch -> PC` / `…, PC -> Switch` / `Use "Get from PC" on Switch to continue there.` / `Use "Send to PC" or "Get from PC" on Switch.` |
| Not found (AC) | `PC save not found: start TotK on PC once or set save folder in Settings` |
| Server card | `Server`, `Starting…`, `Running`, `Error`, `Stopping…`, `Listening on port <port>`, `No requests from the Switch yet`, `Last request: <HH:mm:ss> from <ip>` |
| Port busy (AC) | `Error: port <port> is busy (is the launcher already running?)` |
| Buttons (Phases 1–2) | `Show`, `Hide`, `Copy`, `Save switch-config.json…`, `Firewall help`, `Retry`, `Change port`, `Open folder`, `Open backups folder`, `Open data folder`, `Open network settings`, `Choose folder…`, `Browse…`, `Use automatic`, `Done`, `Close`, `View activity` |
| Game folder error (AC) | `totk.exe not found in <path>` |
| Validation | `Port must be between 1024 and 65535.` / `Keep at least 1 backup.` |
| Snackbar | `Token copied`, `Address copied`, `Command copied` |
| Close during write | `Finishing save transfer` / `TotK Save Sync will close when the save is fully written. This takes a few seconds.` |
| Activity | subtitle `Requests from the Switch, newest first. The last 500 are kept.` / `Loading activity…` / `Couldn't read the earlier activity` + `New requests are still recorded. Earlier entries may be missing.` |
| Empty states | `No requests yet` + `When the Switch connects, its requests show up here. This window must stay open.` / `No backups yet` + `A backup of your PC save is made automatically before the Switch replaces it.` |
| SD path | `sd:/config/totk-sync/config.json` |
| Phase 3 only | `Restore…`, `Restoring…`, `Cancel`, `Restore this backup?`, `Select a backup to restore.`, `Close TotK to restore a backup.`, `Backup restored`, `Couldn't restore the backup` (section 12.1) |

Banner strings: section 8. Activity result strings: section 11. Firewall dialog: section 10. Setup card: section 7.

---

## 16. Build checklist for the programmer

- [ ] No hex colours, no hard-coded font sizes outside the type ramp, no spacing values outside 4.1.
- [ ] All status = icon + word. Colour only on icons.
- [ ] One Primary button per screen state (Play / `Choose folder…` / dialog primary).
- [ ] No blocking work on the UI thread, including activity-log file reads and writes. Async buttons disable themselves and show progress.
- [ ] Token: hidden by default, fixed 16 bullets, never in Activity (screen or file), logs, tooltips or automation names.
- [ ] Activity file keeps exactly the newest 500 entries; an unreadable file shows the warning and never blocks startup.
- [ ] Phases 1–2: no Restore UI anywhere on the Backups page.
- [ ] Keyboard-only run through Home, Settings, Firewall dialog, with focus visible in light and dark.
- [ ] Checked at 760×540, 100/150/200% DPI, light, dark, a contrast theme.
- [ ] Symbol names verified against the installed WPF-UI version.

---

## Developer decisions (2026-11-03)

1. **Navigation:** NavigationView rail (`LeftFluent`) with pages Home / Backups / Activity, Settings in the footer.
2. **Accent:** system accent colour; no brand colour.
3. **Firewall:** no "Run as administrator" button; the dialog gives the `netsh` command + Copy only.
4. **`Open network settings`** link (`ms-settings:network-status`) in the firewall dialog: included.
5. **Port change** applies after restarting the launcher (no live server restart).
   - Слайс 1.3: вместо перезапуска лаунчера сервер перезапускается сам; напоминание пересохранить switch-config.json показывается InfoBar.
6. **Firewall dialog** opens automatically once, right after the game folder is accepted on first run.
7. **Window title / product name:** `TotK Save Sync`.
8. **Activity log persists across restarts:** last 500 entries in a file under `%LOCALAPPDATA%\TotkSync\`, never the token. Page states: loading, loaded, empty, unreadable-file warning (section 11). "Clear log" is not added (reasoning in section 11).
9. **Restore is hidden until Phase 3:** in Phases 1–2 the Backups page shows only the list + `Open backups folder`. The Restore design lives in section 12.1 (Phase 3 only).

[x] Reviewed
