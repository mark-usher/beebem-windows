# Rebase notes, October 2026: FileStore, Master ET and keyboard work onto Codeberg master

Version 1.10, 20 Oct 2026. Written while building the branches; updated after the first Visual Studio builds and the front panel, hardware and FDC work (see the change log).

## Summary

| Branch | Base | Commits | State |
|---|---|---|---|
| `filestore-codeberg` | dccbcc8 | 11 + notes + 21 follow-ups | FileStore E01/E01S ported onto current upstream as logical commits, then the drawn front panel, hard drive unit settings, hardware accuracy from the service manual and the shared 1770/2793 FDC code (see "Changes after the port"). Builds warning-free with VS2019 (Debug x64); needs testing. |
| `master-et-codeberg` | dccbcc8 | 0 | Everything worth keeping is already upstream; branch equals the base. |
| `keyboard-fix-codeberg` | dccbcc8 | 1 | aff61a2 cherry-picked unchanged. Review recommends **not** merging as is. |

- **Base used:** `codeberg-master` = **dccbcc8** "Added FixSerialULARead Preference setting" (14 Oct 2026). Codeberg itself could not be reached when the branches were built, so a newer Codeberg head was not checked. If Codeberg has moved on, rebase the three branches onto it; the FileStore commits touch few upstream lines, so this should be easy.
- **Built by Mark** on SURFACE7 with Visual Studio 2019, Debug x64, with no errors and no warnings (see "Build findings"). Every change is also syntax-checked with MinGW-w64 g++ (`-fsyntax-only`, project defines incl. `NOMINMAX`) first. Four upstream files fail under MinGW even unchanged (BeebWinDx, BeebWinSpeech, Debug, Speech: DirectX/SAPI/MSVC specifics). MinGW's GDI+ headers differ from the Windows SDK's, which hid one MSVC error (22463e5).
- **.vcxproj/.filters** list every new file: `FileStore.cpp/.h`, `BeebWinFileStore.cpp`, `FileStorePanelArt.h`, `Help/filestore.html` (also in the Inno Setup script). `Disc2793.cpp/.h` were removed again by c79ac79.
- **Tools** (not part of the build): `Tools/FileStorePanelArt/` generates `FileStorePanelArt.h` from the Vidaloka font (SIL OFL 1.1, licence included); `Tools/FileStoreIcon/` generates `FileStore.ico`.

## filestore-codeberg

### Approach

The old `filestore` branch (99e3eb5) is 44 commits on dc07d43 (Jan 2022). Its net diff (`git diff dc07d43 filestore`, 12,800 lines) mixes three things:

1. FileStore work (ported);
2. Chris's general work that is upstream now, often renamed or rewritten (RomConfigDialog/RomConfigFile split, Model.cpp, UserPortRTC, Select Hard Drive Folder, CPU-type 6502 emulation, ReportV, DebugDisplayTraceF): not ported, upstream wins;
3. a copy of BeebEm 4.19's Econet code (de2f2ae) and local settings: not ported.

Rather than threading `MachineType == FileStoreE01 || ...` tests through `BeebReadMem`/`BeebWriteMem` and many other places as before, the port isolates the FileStore:

- `Model.h`: `FileStoreE01`, `FileStoreE01S` after `MasterET`, plus `IsFileStore(Model)`.
- `FileStore.cpp` (new): the whole &FC00-&FCFF I/O page, front panel switches, control latch and per-frame polling. `BeebReadMem`/`BeebWriteMem` dispatch FileStore models to it first, so the B/B+/Integra-B/Master paths are untouched.
- `BeebWinFileStore.cpp` (new): window side: drive door, disc menu enabling, front panel drawing, LEDs, shutdown sequence.
- `Disc2793.cpp` (new): the 2793 FDC. Since c79ac79 the FileStore uses `Disc1770.cpp` instead, in WD2793 mode (see "Changes after the port").
- Small hooks elsewhere (`6502core.cpp` PollHardware/PollVIAs/CPU type, `Rtc.cpp`, `Scsi.cpp`, `Econet.cpp`, `BeebWinIo.cpp`, `BeebWinPrefs.cpp`, `Debug.cpp`, menus).

### Commit mapping, old -> new

| New commit | Subject | Old commits it covers |
|---|---|---|
| 83e6a76 | FileStore: add E01/E01S models, model menu and ROM configuration | 3d4b566, 73cfc1e (Chris); 1e9a5e3, f47691b, a5ce913, c1d4e47 (ROM v1.40) |
| 0a62c95 | FileStore: memory map, front panel, system VIA, CMOS and RTC | c1d4e47, afcafb3, 4ddef5b, 458d145 (RTC), 21da1df, d0a59dd, 6e71907; 692a2b6, b1a7a57 (Chris) |
| 7e7b0ac | FileStore: WD2793 floppy disc controller | 1ba32de, 8a42c11, f454d84, 3b71328, d9192d1, 84cd757 |
| 693f29e | FileStore: SCSI hard drives, front panel image and LEDs | 99e3eb5 (recovered SCSI target->drive), 458d145 (graphics, HD); c5a6cc3, 8ea0b46, 82dd9e6, 066669f (Chris) |
| ea47552 | FileStore: flush buffers before reset, exit and model change | d191f05, 480badc, 2d7ae83, a87c378, 542843f |
| 66fb1f3 | FileStore: Econet integration | de2f2ae (FileStore parts), f3b91b3, 2a5fcd6 |
| fa69bfb | FileStore: draw the front panel instead of using bitmaps | new (Mark's request) |
| a2b1f5f | Annotate the RTC register table with value ranges | archive/rtc-comments 70aeaa0 |
| 0e055f4 | Help: add FileStore page and menu entries | 458d145 (Help/rtc.html text) |
| 29ec95c | FileStore: fixes from code review of the port | new (independent review of this port) |
| d61b0ca | FileStore: disable the door menu item while a shutdown is pending | new |

Authorship: mark-usher on the ported commits, with `Co-authored-by: Chris Needham` where his commits are folded in; fa69bfb is new work. Each message names the original hashes.

Old commits not carried over (already upstream, superseded or not FileStore):

| Old | Why |
|---|---|
| 6c6506e (Chris) Select Hard Drive Folder | Upstream as 0d73a7e |
| 3652835, 25e9ca3, de71ffd, 604ae6d, 54cbbfc, a5b8597, 44984be (Chris) | Clean-ups of code that upstream has since rewritten; their FileStore content is in the commits above |
| aa21b3a (Chris) CPU speed regulation | Re-implemented: FileStorePoll() signals a frame every 40,000 cycles, as before |
| 2a5fcd6 SendLen | Upstream already initialises it |
| de2f2ae Econet 4.19 copy | Upstream's Econet rewrite replaces it; only the FileStore address mapping and CMOS station write are ported |
| 0f3a4a5, .gitignore, CLIPBOARD | Local files |
| ca5ab59 variable spelling | Upstream renamed it (EconetNMIEnabled) |
| UserData/Econet.cfg, Preferences.cfg changes | Mark's local network (192.168.1.x, AUNMODE 0) and personal settings |
| disc1770.cpp changes | Reverted a later upstream refactor (CurrentDiscOpen); FileStore LED calls in it were dead code (FileStore uses the 2793) |
| DiscType::AFS0 | Declared but never used |
| Debugger "f" (5-byte) watch type | Not FileStore-related, and shifts an int by 32 (undefined behaviour); left out |
| log.cpp | Excluded by the brief |

### Conflicts and how each was resolved

1. **Model enum values.** Upstream has `MasterET = 4`; the old branch had `FileStoreE01 = 4, FileStoreE01S = 5`. FileStore models are now 5 and 6, `MODEL_COUNT` 7. Preferences now store the model as a string ("FileStoreE01"/"FileStoreE01S"); the legacy binary values 4/5 written by the old branch are mapped to the FileStore models, so an old FileStore Preferences.cfg still opens as a FileStore.
2. **CPU type.** Upstream's `CPU::CPU65C12` replaces the old `CPU65C02`; FileStore models use it (the old branch used the 65C02/Master tables too).
3. **ROM config.** Upstream's Roms.cfg is 17 lines per model, with defaults for models missing from older files. The FileStore sections use that format (E01: OS, FS, 15 x EMPTY; E01S: combined ROM, 16 x EMPTY) instead of the old 3 extra lines. Existing Roms.cfg files without them get the defaults automatically. The ROM dialog shows only the OS/FS rows for FileStore models. ROMs live in `UserData/BeebFile/FILESTORE-E01(S)/`.
4. **RTC.** Upstream refactored the 146818 into `Rtc.cpp` (CMOS.Register[2][64], RTCWriteAddress/RTCWriteData/RTCReadData). Extended to four CMOS banks (Master 128, Master ET, E01, E01S); FileStore defaults as on the old branch (station 254, check byte, max users 128, max drive 6, printer "Puce", user "SYST", SIN &FFFFFF, byte 49 = 1). Preferences keys `CMOSRamE01`/`CMOSRamE01S` (as the old branch). Upstream did not emulate the update cycle, flags or interrupt; added for FileStore (see behaviour changes). `DebugRTCState()` was declared upstream but not defined: implemented, debugger `state c`.
5. **Econet.** Upstream rewrote Econet.cpp (AUN, error handling). The FileStore only needed the address mapping (&FC20-23 ADLC, &FC24 INTOFF/station, &FC28 INTON) and the CMOS station write in EconetReset(); both fitted to the current API (EconetRead/EconetWrite/EconetReadStationID/EconetInterruptRequest/NMI_ECONET).
6. **Logging.** Old `WriteLog()` calls (unconditional in scsi.cpp) are now upstream-style: `// #define DEBUG_2793`, `// #define DEBUG_SCSI` with `DebugTrace()`, Econet traces under upstream's `DEBUG_ECONET_INTERRUPTS`, RTC under `DEBUG_RTC`, and `DebugType::CMOS` instead of the old `DebugType::RTC` (upstream already has CMOS trace/break checkboxes).
7. **Timers.** The old branch used Windows timer IDs 3 (RTC) and 4 (shutdown); upstream now uses 3 and 4 for the printer and device-lost timers. The RTC no longer uses a timer; shutdown uses new `TIMER_FILESTORE_SHUTDOWN = 6`.
8. **File names.** upstream renamed files (beebwin.cpp -> BeebWin.cpp etc.); new files follow that: `Disc2793.cpp/.h` (was disc2793.*), `FileStore.cpp/.h`, `BeebWinFileStore.cpp`. No stale lower-case names or old identifiers remain (checked by grep and compile).
9. **LEDs.** Upstream's `LEDs.Disc0/Disc1` are now `LEDs.FloppyDisc[2]`; the old branch used `LEDs.Motor` for the MODE LED (upstream's cassette motor LED), now a separate `FileStoreModeLED`.
10. **Disc handling.** Upstream keeps disc names in `DiscInfo[]`; the 2793's own file-name table was dropped.

### Behaviour changes to check (deliberate)

- **RTC update cycle** is driven by emulated CPU cycles: 20,000 cycles = 10 ms, the same period as the old Windows timer (so it now stops when paused and scales with emulation speed). A real 146818 updates once a second; see open questions.
- **RTC IRQ masks fixed.** The old `HandleRTCTimer()` tested `RegB & 10/20/40` in decimal. In effect it raised the IRQ whenever UF was set and UIE **or DM (binary mode)** was set. Now: IRQF/IRQ when (UF and UIE) or (AF and AIE) or (PF and PIE), as the datasheet.
- **Register C** now returns its value and clears all flags and the IRQ on any read (old: only when it was exactly &90; otherwise returned 0 without clearing).
- **SET bit** still does not move the FileStore clock (it always follows the PC clock), as before.
- **Frame timing / speed regulation**: countdown of 40,000 cycles per frame, as before.
- **FDC:** behaviour preserved, including that every Type I/II/III command waits for the 2793 spin-up delay (the "no motor-on bit" emulation). TR00 is never inverted in FileStore mode (old code could inherit the Watford board setting). `CurrentDisc` is cleared when its drive closes (was left dangling). Since c79ac79 this is `Disc1770.cpp` in WD2793 mode, so the FileStore also gets the later upstream 1770 fixes.
- **E01 OS ROM at &8000-&BFFF**: the old BeebMemInit copied sideways ROM bank 15 over the first 16K of the E01 OS ROM after loading it; that copy is skipped for the FileStore now, so the whole 32K ROM image stays. (Worked before because that half of E01-v140-MOS.rom is almost empty: 16,344 of its 16,384 bytes are &FF, only 40 bytes are data.)
- **Write Protect on Load** is not applied in FileStore mode (old code switched the preference off instead).
- **Freeze when inactive** is ignored in FileStore mode (old code switched the preference off).
- **Display renderer/window size** are no longer forced to GDI and 640x512 (the old code changed those preferences for every model). The panel is drawn with GDI whatever the renderer.
- **Econet** is still forced on in FileStore mode, but the user's own setting is kept for other models and saved to Preferences.cfg (the old code overwrote it). The Econet menu item is disabled in FileStore mode.
- **Shutdown**: reset/exit/model change open the door, wait 2 s, then act; door closed afterwards. File > Reset, Break (F12), Exit/close, model change and Select Hard Drive Folder's "reset now" go through this. Loading preferences and Windows log-off/shutdown are not intercepted. A pending shutdown is cancelled if the model is changed some other way, and the door menu item is disabled while one is pending.
- **Tube/co-processors** are not run in FileStore mode (old code skipped initialising them but still ran them, which could crash with an ARM/Z80 setting).
- **Not available in FileStore mode** (menus disabled): Run Disc, tape, serial, Music 5000, AMX, teletext, IDE, user port breakout/RTC, joystick, key maps, copy/paste, screen/video capture, Tube, disc import/export (DFS only), **Save/Load State** (the UEF state format does not cover the FileStore hardware). Added later (96b4a9d, 6be08f6): Translate CR-LF, View > LEDs, Motion Blur, Allow SW RAM Write, Solidisk SW RAM Board, Floppy Drive On/Off, Disable Keys; sub-menus whose items are all disabled are greyed too.
- **User port devices** (AMX mouse, User Port RTC, breakout box) are ignored in FileStore mode, because BeebEm's user VIA is the FileStore's system VIA. The printer output stays connected.
- **RTC address** written to &FC00 (and by the Master's system VIA) is masked to 6 bits, as on the 146818. Before, an address of &40 or more read or wrote past that model's CMOS RAM.
- **Floppies** loaded in FileStore mode are writable unless the image file is read only.
- **Discs given on the command line** or dropped on the window are loaded even with the door closed (deliberate: it is how a FileStore would be set up at start-up).
- **Front panel** is drawn (no bitmaps); see the next section.

### Bugs fixed while porting

- Infinite recursion: File > Reset with the door already open (CloseFileStore -> ShutdownFileStore -> ResetBeebSystem -> CloseFileStore ...) because the "timer elapsed" flag was only set by the timer.
- Decimal bit masks in the RTC IRQ logic (above).
- Tube co-processor executed in FileStore mode without being initialised.
- SCSI trace format errors (`%d` with missing args, `&d`).
- After review: Break (F12) reset the co-processor (null pointer with an ARM Tube setting) instead of the FileStore; Quick Save/Load shortcuts and dropped state files bypassed the disabled menus; a failed disc reload left `CurrentDisc` pointing at a closed file; floppies could load write-protected from a stale shared flag; AMX/User Port RTC could drive the FileStore system VIA.
- The old write path set `LEDs.ShowKB/ShowFS/ShowDisc/CapsLock` on every unhandled I/O write ("temporary"); removed.

### Front panel (new, fa69bfb; redrawn from photos since)

At Mark's request the three 24-bit BMPs (about 465 KB) are replaced by a drawing scaled to the window, refined from Mark's photos of real units (ff41e68 to 4aa562f):

- The FileStore at the top with one hard drive unit per attached SCSI drive stacked underneath (none for a floppy-only FileStore). The window height is fitted to the units, and the window size used by other models is kept and restored.
- Cream case and recessed base; see-through amber flap over a very dark black-brown fascia, with a bevelled edge and 60 evenly spaced stripes on a whole-pixel pitch.
- Black model label ("FILESTORE E01", "E01 S", "E20", "E40 S", "E60 S" or a custom label, with a half space before a trailing S) and Acorn badge. Lettering is Vidaloka (chosen by Mark), compiled in as outlines because GDI+ cannot load CFF OpenType fonts; the Acorn logo is traced from a photo of a badge. Both are in `FileStorePanelArt.h`.
- Door open: two floppy bays, each a blank panel and a 3.5" drive, with a black shutter when empty or a thin filled-in disc when loaded, and the lime activity LED. The LEDs glow through the closed door.
- MODE (red) and POWER (green) LEDs with tan labels; on hard drive units the red DRIVE LED shows disc activity and is always red (the View > LEDs colour does not apply).
- Drawn into a 32-bit DIB section with per-pixel blending, GDI+ for text and logo; redrawn when an LED or disc changes (checked once per emulated frame), on door/drive changes and on resize.

### Changes after the port (19-20 Oct 2026)

| Commits | Change |
|---|---|
| ff41e68, 7654fea, c1ffe55, 966fbc1, a784f09, 6e5713c, 93446bf, 726bd12, 4aa562f | Front panel redrawn from Mark's photos (see "Front panel"); Vidaloka lettering and traced Acorn logo |
| 22463e5, 1a12875, 2d128f2, 998e5db | Build fixes and warnings (see "Build findings"); style tidy-up to match the BeebEm code |
| 91ca359, 6be08f6 | Hardware > FileStore Hard Drive Units: each of drives 0-3 is None, Auto (attached if `scsiN.dat` exists, labelled from its size), E20, E40S, E60S or a custom label of up to 4 characters. Saved as `FileStoreDrive0`-`3` in the preferences file; a custom label is set there (any value that is not a keyword). A drive set to None is not attached, and a missing image for an Auto drive no longer gives an error. Changing a drive restarts the FileStore after flushing. |
| 6be08f6 | File > FileStore Command Mode removed; &FC2C bit 7 reads as off. `BeebWin::EnableSubMenu()` greys a sub-menu found from an item in it (by position, as sub-menus have no command ID). |
| 96b4a9d, bd91b03 | More menus disabled in FileStore mode; floppy drives cannot be switched off; the I/O page is not slowed to 1MHz (the FileStore's bus runs at 2MHz); DRIVE LED always red |
| 998e5db | Fix: VIA writes were stretched twice |
| 436db67 | From the service manual: the 6522A is clocked at 2MHz, so its timers count at 2MHz on the FileStore (`UserVia.cpp`, counts halved for the interrupt lead time); page &FC repeats every &40 and the RTC, latch, INTON/INTOFF and switch registers answer on 4 addresses each; DRAM refresh holds the CPU for one cycle at the first opcode fetch after every 30 cycles (about 3% slower than a plain 2MHz) |
| 96b4a9d, c79ac79 | Fix from upstream: the "confusion spin" time-out was an unbraced `if` in the 2793 copy |
| c79ac79 | `Disc2793.cpp/.h` removed. The FileStore uses `Disc1770.cpp` with `Set1770FDCType(FDCType::WD2793)`, set in `ResetBeebSystem()`. WD2793 differences kept from the original port: no Motor On status bit (bit 7 is Not Ready, and the drives are always ready), so every command waits for the head to load; index bit cleared when idle; density check on Type I commands and not on Read Address; settle 60,000 and revolution 333,333 cycles; floppies cannot be disabled. `BeebWin::Load2793DiscImage()` is now `LoadFileStoreDiscImage()`; write protecting a FileStore disc now acts on the drive the FileStore uses. |
| 1a4c98b | `FileStore.ico` for a shortcut |

References used (on the `reference-docs` branch, not for merging): FileStore Service Manual v2 (Mark Ferns / Mark Usher, 2010) and the WD279X-02 data sheet.

### Build findings (SURFACE7, Visual Studio 2019)

- `DXSDK_Dir` must be set in `Src/BeebEm.user.props` (copy of `BeebEm.user.props.example`).
- `afxres.h` not found: install the MFC component of VS2019.
- Set BeebEm (not ZipFile) as the startup project.
- Opening the solution in VS2019 retargets the projects from `v140_xp` to `v141_xp`, which gives warning MSB8051 (XP support deprecated). Retargeting locally to `v142` removes it; this stays a local change, the repo still targets VS2015.
- `gdiplus.h` needs `using std::min/max` before it (NOMINMAX), as elsewhere in BeebEm (22463e5).
- Upstream warnings fixed: C4267 in Debug.cpp (1a12875); C4996 from `sphelper.h` with the Windows 8.1 SDK (2d128f2).
- A Rebuild empties `Src\x64\Debug`; the `.snd` samples and `Teletext.fnt` are not always copied back by the build. Copy them by hand if BeebEm reports them missing.
- Run with your own data folder, e.g. `-Data "C:\BeebEm-FileStore\TestData" -Prefs FSE01.cfg -Roms Roms_FS.cfg -ecostn 243`, so the repo's UserData stays upstream.

### Questions for Mark (FileStore)

1. **RTC update period.** Keep 10 ms (as the old timer) or use the real 1 s? If the FileStore ROM counts update-ended interrupts for its time-outs, 10 ms makes them 100x faster. *Resolved (20 Oct): the FileStore depends on timings, so the 146818 is now emulated as the data sheet: a 32.768kHz time base in CPU cycles, one update cycle a second (UIP 244µs before, UF at the end), the periodic interrupt at the rate set in register A, and the alarm. A FileStore reset acts like the RESET pin; register D reports a good battery.*
2. **E01 CPU.** The E01 has a Rockwell 65C102 (with BBR/BBS/RMB/SMB). BeebEm emulates the Master's 65SC12 instruction set for it, as before. Does the FileStore ROM use the Rockwell bit instructions? (No problem was seen on the old branch.) *Resolved (20 Oct): a search of the FileStore source (Mark's source-history repo, all files) finds no RMB/SMB/BBR/BBS, so the 65SC12 emulation is sufficient.*
3. **Door and SET bit.** Should the FileStore be able to set its own clock (`*FSDATE`-style), i.e. honour the SET bit like the Master? Currently not, as before. *Resolved (20 Oct): the clock can be set on every 146818 machine (as upstream on the Master), kept as an offset from the PC clock. New option Hardware > RTC Clock Offset, for all models: when ticked, the offset is kept over a reset and saved as `RTCClockOffset` (seconds) in the preferences file; when not, the clock goes back to the PC clock on a reset (upstream behaviour). The FileStore clock is normally set with a CMOS utility (see WS-7).*
4. **System VIA write timing.** *Resolved (20 Oct):* the FileStore's I/O bus runs at 2MHz (Mark; service manual), so FileStore I/O accesses are no longer stretched at all (96b4a9d), and the 6522A's timers count at 2MHz (436db67).
5. **Station number.** EconetReset() overwrites FileStore CMOS bytes 14/15 with the station from Econet.cfg / `-EcoStn` on every reset, as before. Keep? *Resolved (20 Oct): yes, always from Econet.cfg or -EcoStn. Refined on filestore-econet: with econet-reworking-2's AUTOCONFIGURE, the FileStore takes its station from its own CMOS (byte 14, saved as CMOSRamE01/E01S) when -EcoStn isn't given, as the Master does.*
6. **Save state** is disabled for FileStore. Worth adding later (64K RAM, VIA, RTC/CMOS, 2793, SCSI, front panel)? *Resolved (20 Oct): no; the FileStore should be shut down properly.*
7. **WD17xx subclassing.** *Resolved (20 Oct, c79ac79):* Mark chose an FDC type rather than a class. `Disc1770.cpp` has `Set1770FDCType(FDCType::WD1770 / WD2793)`; the WD2793 keeps the differences listed in "Changes after the port". The control register needed no change (the FileStore latch is mapped to the 1770 layout in FileStore.cpp).
8. **Knowledge sheets.** Facts worth writing back to the Acorn knowledge sheets: FileStore memory map &FC00-&FC33 as implemented here, repeating every &40 with 4-byte aliases (service manual 4.13.3); control latch &FC08 bits; front panel switches &FC2C (b6 door, b7 command E01; SW3, the COMMAND/USER input, is the optical door sensor, Mark 20 Oct); SCSI drive = selected target ID bit (1/2/4/8 -> 0-3); RTC update-ended IRQ used by the FileStore; CMOS defaults above; 65C102 at 2MHz from an 8MHz crystal; 6522A VIA on φ2 (2MHz); DRAM refresh holds the CPU for one cycle at the first opcode fetch after every 30 cycles (refresh every 15µs); 2793 at 250kbps MFM (1MHz FDC clock, so the 1770's step and settle times apply).

### Manual test plan (Windows, after a Visual Studio build)

Use a copy of the UserData folder so your normal Preferences.cfg is not changed.

1. **Start-up:** Hardware > BBC Model > Acorn FileStore E01. Expect the drawn front panel; MODE LED behaviour as on the real unit during start-up; no error about ROMs.
2. **Old preferences:** start with a Preferences.cfg saved by the old FileStore build (MachineType=04 or 05). Expect it to come up as E01/E01S, with its CMOSRamE01/E01S kept.
3. **Econet:** with Econet.cfg giving the FileStore a station (e.g. 254), log on from an emulated or real station (`*I AM SYST`), catalogue, save and load a file. Check the station number in CMOS with the debugger: `state c` (bytes &0E/&0F).
4. **SCSI drives:** Hardware > SCSI Hard Drive on, with scsi0.dat (and scsi1.dat) in the hard drive folder. Expect one drive unit per image under the FileStore, labelled by size, red LED flashing on access, and files on both drives reachable. (This is the 99e3eb5 target ID mapping.)
5. **Floppies:** File > FileStore Door Open; expect the drive picture, Load Disc enabled. Load an ADFS/AFS0 floppy into drive 4, close the door, and use it from a station (`*CAT`, `*SDISC`). The floppy LED should glow through the closed door during access. Try loading an .ssd: expect "The FileStore can only use ADFS discs". (Repeat after any change to Disc1770.cpp, which now serves both machines.)
6. **Hard drive units:** Hardware > FileStore Hard Drive Units: set drives to None/Auto/E20/E40 S/E60 S; the FileStore restarts after flushing, and the units shown follow the settings. Put `FileStoreDrive2=CUST` in the preferences file and check the custom label and its menu item.
7. **Shutdown flush:** write a file from a station, then File > Reset. Expect the door to open, a 2 s pause, then the reset, with the file intact afterwards. Repeat with File > Exit, with F12, and with a model change to Master 128. With the door already open, File > Reset should reset at once (this used to recurse and crash).
8. **RTC:** `*TIME`-style date/time on a station from the FileStore matches the PC clock; the FileStore keeps working for more than a few minutes (RTC interrupt, see open questions 1 and the IRQ mask change).
9. **Inactive window:** with Options > Freeze when inactive ticked, switch to another window; the FileStore must keep serving. Switch to Master 128 and check the option still works there.
10. **Other models unaffected:** Model B with DFS, Master 128 with ADFS floppy, Master ET with Econet: load and run discs, Save/Load State, AMX mouse, User Port RTC. Expect no change from upstream. Econet on/off setting for those models must be what it was before using the FileStore.
11. **Debugger:** with the debugger open on a FileStore, tick CMOS trace; expect RTC reads/writes in the log. `state c` shows the clock registers.
12. **Renderers:** try the FileStore with GDI, DirectDraw and DirectX9 renderers and resize the window; the panel should redraw cleanly in each.
13. **Start-up sequence** (service manual 5): power and MODE LEDs light, any hard drive unit's LED flickers, the floppy LED lights, then only power stays on.
14. **Timing:** the FileStore clock keeps time, and printer/Econet time-outs behave as on the real unit (VIA timers now at 2MHz, refresh wait states added).

## filestore-econet (FileStore with econet-reworking-2)

On 20 Oct 2026 Mark fetched Codeberg: **master is still dccbcc8**, so filestore-codeberg, master-et-codeberg and keyboard-fix-codeberg are already on the current upstream master and need no rebase. Copies of the Codeberg branches are on GitHub as `codeberg-master-20261020` and `codeberg-econet-reworking-2`.

`econet-reworking-2` (4c0b74b, 116 commits by Chris Needham and ZXGuesser) reworks Econet: I/O completion port sockets (`UdpServer`), `EconetConfig`, station auto-configuration and discovery, gateway discovery, an Econet timer, and an Econet Network dialog with a log. It branched from master before dccbcc8 (merge base c1e0ea3).

`filestore-econet` = filestore-codeberg + a merge of econet-reworking-2 (4595eb1). Conflicts and how they were resolved:

| File | Conflict | Resolution |
|---|---|---|
| Resource.h | FileStore menu IDs 40327-40329 clashed with IDM_ECONET_NETWORK, IDM_COPY, IDM_SELECT_ALL | FileStore IDs moved to 40330-40357 |
| BeebWin.h | TIMER_ECONET and TIMER_FILESTORE_SHUTDOWN both 6 | TIMER_FILESTORE_SHUTDOWN = 7 |
| BeebWin.cpp SetModel() | FileStore shutdown vs `PreferredStationID = 0` | Shutdown first, then clear; also cleared when the delayed FileStore shutdown changes the model |
| BeebWin.cpp ResetBeebSystem() | FileStore Econet forcing vs `ResetEconet()` | Force Econet on for the FileStore, then `ResetEconet()` (starts the Econet timer) |
| BeebWin.cpp timers | Both new timers | Both handled |
| BeebEm.rc | Chris's new menu accelerators vs FileStore menus | Both; accelerators added to the FileStore and RTC items (Acorn &FileStore E01, E01&S, FileStore Hard &Drive Units, RTC &Clock Offset) |

The FileStore needed no code changes for the new Econet: it only uses `EconetRead/Write`, `EconetReadStationID`, `EconetInterruptRequest`, `EconetNMIEnabled`, `EconetEnabled` and the CMOS station write in `EconetReset()`, all unchanged. Station number (Mark, 20 Oct): the FileStore always takes its station from its own CMOS (byte 14, saved as CMOSRamE01/E01S, default 254), with or without AUTOCONFIGURE. `-EcoStn` overrides it for the session only: it is written into CMOS so the FileStore ROM sees it, but the original bytes 14/15 are saved in the preferences file (`RTCSetCMOSByte(..., Temporary)`); a change the FileStore makes itself is permanent. Without AUTOCONFIGURE, if Econet.cfg has no entry for that station on this PC, an entry is added in memory for the session (port 10000 + 256 x net + station), so nothing is written to or removed from Econet.cfg. Master behaviour is unchanged (Mark to discuss -EcoStn with Chris).

Fixes to Chris's code needed for the VS2019 v142 build (to pass on to Chris): `UdpServer.cpp` WSASocket -> WSASocketW (C4996 is an error with SDL checks); `Debug.cpp` lines 1319/1331 static_cast<int> of SendMessage (C4244).

Bug in econet-reworking-2 found in testing (to pass on to Chris): with a fixed `GATEWAY` and `FINDGATEWAYS 0`, `EconetReset()` never started the gateway keep-alive, although its comment says it should; the keep-alive timer was only set inside `if (EconetConfig.FindGateways)`. A station that only answers (such as a FileStore) then never sends anything through the gateway, so the Pi Econet Bridge never switches it to gateway mode and real stations get "not present". Fixed: the keep-alive starts whenever a gateway is known, and discovery only when `FINDGATEWAYS` is on and none is known. A failed bind to a configured station's port is now also written to the Econet log, since BeebEm otherwise moves silently to the next configured station for this PC.

Prefs files written by the old `filestore` branch (no `PrefsVersion`) store `MachineType` as a number with that branch's numbering (4 = E01, 5 = E01S). Hand-edited files using the new enum values (5 = E01, 6 = E01S, 4 = Master ET) load as the wrong machine; open each in BeebEm, choose the model and save the preferences to convert it to version 3.

Tested with the real Econet on 20 Oct 2026: SURFACE7 runs BeebEm on its own net 7 (Master 7.105, FileStore E01 7.243, E01S 7.244; Econet.cfg `DEFAULTNET 7`, `GATEWAY 192.168.0.62 16384`, ports 3<net><station>, e.g. 37243). The Pi Econet Bridge on apt-office-pieb-wifi maps those three stations with `AUN MAP HOST 7.x ON 192.168.0.10 PORT 37x NONE` and runs the AUN gateway, which can only be set in its JSON config (`"aun-gateway": "192.168.0.62:16384"` in `general`); the bridge reads the JSON only while it is newer than `econet-hpbridge.cfg`. A BBC on the real Econet (2.21) logs on to the emulated FileStore at 7.243.

Changes on this branch since v1.8, each also offered to Chris on Codeberg as a pull request against his branch (Mark opened the PRs from his fork, markusher/beebem-windows):

| Commit here | PR branch (GitHub) | Codeberg PR base | Change |
|---|---|---|---|
| 61f1c06 | `pr/econet-gateway-keepalive` | `econet-reworking-2` | Gateway keep-alive also sent when `GATEWAY` is set (above) |
| 06129f5 | `pr/makefilename-separator` | `master` | `MakeFileName()` checked the string terminator instead of the last character, so it always added a separator (`...\\txt0.dat` in the preferences) |
| b1a907d | `pr/econet-gateway-stations` | `econet-reworking-2` | Econet Network dialog: stations heard through the gateway are listed after the others in the Stations list, with the gateway's address and port, in the system hot-track colour (blue). Tested: 2.21 appears when it logs on to 7.243 |

The failed-bind log line (61f1c06) is only on this branch.

History rewrite (20 Oct 2026): at Mark's request, the commits on `filestore`, `keyboard-fix`, `archive/rtc-comments`, `filestore-codeberg`, `filestore-econet`, `keyboard-fix-codeberg` and `reference-docs` were rewritten so that mark-usher is the author and committer throughout, with no co-author lines, and every version of these notes was reworded to match. Only these notes changed; the code in every commit is identical. Commit hashes quoted in these notes are the rewritten ones. Clones made before the rewrite must be moved with `git reset --hard origin/<branch>` (or `git branch -f`), not pulled.

Test on this branch: everything in the FileStore test plan, plus Hardware > View Econet Network with the FileStore running (log shows its traffic), AUN gateway/discovery if used, and a Master/Model B station logging on to the FileStore over the new code.

## scsi-pi1mhz (SCSI drives as Pi1MHz emulates them)

Separate branch for testing, on `filestore-econet` (so with econet-reworking-2 and the FileStore). The SCSI drives now behave as Pi1MHz's BeebSCSI does: drive controllers, mode pages and the `scsiN.cfg` drive descriptors. The reference is dp111/Pi1MHz master (0e353a6, 18 Oct 2026, with Mark's PRs on block size and mode pages) plus Mark's unmerged `pr/drive-controller` and `pr/drive-geometry` (Pi1MHzFS, 19 Oct).

| File | What |
|---|---|
| `Src/BeebSCSI/scsi.cpp/.h` | Pi1MHz `BeebSCSI/scsi.c` line for line, without the LV-DOS (VFS) LUNs, F-codes, jukebox and BeebSCSI group 6 commands (BSSENSE, BSSELECT, FAT transfer). Two BeebEm hooks: `hostadapterTargetAttached` in BUS FREE, `scsiCurrentLun` for the LEDs. |
| `Src/BeebSCSI/filesystem.cpp/.h` | The drive half of Pi1MHz `BeebSCSI/filesystem.c` (LUN start/stop, `.cfg`/`.dsc` descriptors, geometry, mode pages and masks, sector I/O), on the C library instead of FatFs. Drives 0-7 in the hard drive folder; `defscsi.cfg` from the hard drive folder, the user data folder or the program's UserData folder. |
| `Src/BeebSCSI/fileparser.cpp/.h` | Pi1MHz `rpi/fileparser.c` (the `.cfg` reader/rewriter that keeps comments). |
| `Src/Scsi.cpp` | Rewritten: the Acorn host adaptor registers as Pi1MHz's `harddisc_emulator.c` has them, running BeebSCSI as a Windows fiber that waits for the host's access to the data and select registers. A FileStore drive slot set to None (or Auto with no image) does not answer its selection. |
| `BeebWin.cpp`, `BeebWinPrefs.cpp`, `BeebEm.rc`, `Resource.h` | Hardware > SCSI Drive Controller (Mixed, ACB-4000, ACB-4070, SASI, SCSI Drive) and SCSI Drive Geometry (Auto, From the .cfg File Only); preferences `SCSIDriveController`, `SCSIDriveGeometry`. IDs 40358-40364. |
| `UserData/defscsi.cfg` | Pi1MHz's shipped default descriptor, unchanged. |
| `Tools/BeebSCSITests/` | Linux host tests: Pi1MHz's own `test_beebscsi.c` run against the port (103 pass; the 2 BSSENSE/BSSELECT checks fail by design), and register-level tests of the host adaptor (17 pass). `run_tests.sh <Pi1MHz tree>`. |

Fixed after the first test on SURFACE7 (f9f1e05): the data register keeps the byte the host writes. ADFS (&9A6C) and the FileStore MOS (&FA1E) check for the host adaptor by writing &5A/&A5 (&53/&A5 on the FileStore) to the data register and reading it back; without it ADFS went straight to the floppy drive and the FileStore stopped with error &65.

Preferences: version 3.1 is written (all three FileStore branches, Mark 20 Oct): 3 plus the FileStore and SCSI keys. BeebEm reads only the number before the point (`std::stoi`), so upstream still reads such a file as version 3.

Behaviour changes to check: a drive is started by START UNIT or by its first READ/WRITE, as on Pi1MHz, so TEST UNIT READY before that is "not ready" (the old code reported every opened image ready); the old `.dsc`-only drives still get their geometry from the `.dsc`. The `BeebSCSI` objects build into `$(IntDir)BeebSCSI\` because `scsi.cpp` and `Scsi.cpp` would both be `scsi.obj`. Licence: BeebSCSI and Pi1MHz are GPL v3 or later, BeebEm GPL v2 or later, so a build with them is GPL v3 (to raise with Chris before any PR).

## master-et-codeberg

**Result: no commits needed.** `master-et-codeberg` is at the base, dccbcc8 (the `codeberg-master` snapshot, 14 Oct 2026; Codeberg itself was not reachable when the branches were built). Each of the 118 commits on `master-et` (cd4715b, merge-base 306e01c) is either already upstream, superseded by later upstream work, or dropped on Mark's instruction. Nothing was left to carry over.

Method: commits were matched to upstream by exact subject, since no patch-ids match after Chris's rebase onto renamed files. The net change was then checked as well: every line that `master-et` adds since 306e01c was compared with the upstream version of the same file. Only three files still had lines that upstream lacks, and all three are explained below: Econet.cpp (0b23a94, dropped), UserPortRTC.cpp (the c56d5e1 year fallback, skipped) and BeebWin.cpp (two timer constants that upstream defines elsewhere, via Chris's 500f535).

### Commit mapping (mark-usher commits)

| master-et | Upstream | Subject | Outcome |
|---|---|---|---|
| 8f95a37 | ae3404c (Chris) | add new models for emulation to models structure | Superseded: upstream added only `MasterET` to `Model`, not MasterCompact or FileStore. The FileStore models belong on `filestore-codeberg`. |
| 7fda01b | b1104ba (Chris) | updated 6502 emulation to depend on CPU type | Superseded by Chris's version (the master-et copy is 7000400) |
| 029daa3 | (in b1104ba) | fixed error "MachineType ==" missing | Fix for 7fda01b's `|| Model::MasterCompact` bug. Upstream now calls `Poll1770()` unconditionally where appropriate. |
| 12275c4 | (merge) | merged changes to ROM config from Master | Merge of upstream work; nothing of its own |
| e2b1e3e, 2e75a3f | 6c1d5f0 | Updatad CMOS and RTC and preferences ... | Upstream |
| 4d27611 | 724e039 | ensure ROMS for all models are saved | Upstream |
| c920fe9 + ad96ce3 | — | add menu item / revert | Cancel each other out |
| d72c773 | a5d682a | Add Master ET to model menu | Upstream |
| b059b0c, 3b5bdf5 | 7a9c857 | Enable MasterET in memory and settings | Upstream |
| 28298d0 | 505c60b (Chris) | About dialog "Master Series" | Upstream |
| a67f42e | 83027cb | window title "Master Series" | Upstream |
| e3abb1b | 8723b71 | menu options by model capabilities | Upstream |
| 03eb3f1, b33372c | 7fee6d2 | CMOS defaults: econet station 1 | Upstream (later changed by Chris, see below) |
| 0b23a94 | 36bc684 | improved econet error handling | **Dropped** as instructed. Note that an equivalent commit, 36bc684 "Improved econet error handling", *was* merged upstream in May 2024; its `LastError` logic survives in upstream's rewritten Econet.cpp. |
| c56d5e1 + d0b211b | — | User Port RTC override | **Superseded**, see below |
| 6fea847 | — | Master Series CMOS defaults "recommended by Acorn" | **Dropped** as instructed |
| a23aec2 | — | clipboard buffer 64k | **Dropped**: obsolete, see the clipboard recommendation below |
| 9e28783 | 0ec58cd, fb57303 | Master ET CMOS byte 19 comment | **Superseded**: upstream already has the annotated tables with "Default language ROM: 13, Default filing system ROM: 14" |
| f958f89 | various | fixed html pages from master branch | **Superseded**: every fix (`</ul>` in contents.html, `</p>` in econet.html, the stray `</a>` in emulation.html and mastercopro.html, `</td>` in keyboard.html, the Z80 v1.21 paragraph, the "Second Processor" section in menus.html) is already upstream. All seven pages parse with balanced tags. The remaining lines in f958f89 are older wording that upstream has since rewritten. |
| cd4715b | — | merge with current master | Merge; nothing to carry |

### Chris Needham commits on master-et (all already upstream)

All 93 non-mark-usher commits have an upstream commit with the identical subject. Patch-ids differ only because Chris rebased them upstream; for repeated subjects such as "Refactored 6502 opcodes" or "Fixed compiler warnings", the upstream hash shown is the newest commit with that subject. The net-diff check above confirms that none of their content is missing.

<details><summary>Table (93 rows)</summary>

| master-et | upstream | subject |
|---|---|---|
| 10176cc | a57c1bd | Update help files to use modern HTML |
| a093897 | 077cf2b | Format UI text consistently in the help documentation |
| bf4721d | 51d2c6e | Format commands in the help documentation |
| 68b58f1 | 9306f3b | Format keypresses in the help documentation |
| 7a4343a | 9306f3b | Format keypresses in the help documentation |
| 82f58ea | fd39f75 | Format filenames and more commands in the help documentation |
| db16e26 | aad0867 | Updated IntegraB help documentation |
| c85d5e6 | 3c1821d | Added more help documentation |
| 4f9e889 | 3020b4e | Added CONTRIBUTING.md |
| 1ba2834 | 865fc8a | Updated CONTRIBUTING.md |
| 287d92f | 865fc8a | Updated CONTRIBUTING.md |
| e4d460c | 51d87ee | More help documentation updates |
| d6d1c96 | 701bb72 | Added help documentation for keyboard links settings |
| 2c6c724 | 493e0d9 | Added dialog box images to help documentation |
| 771c617 | f712bb4 | Added ZipFile folders to .gitignore |
| 500f535 | bd580b2 | Added timer constants |
| 85c218d | 1524861 | More help documentation improvements |
| 1134f17 | b34dd43 | Reworded help text for consistency |
| f0d5be8 | 5068de8 | Updated Edit ROM Configuration dialog box |
| fe521b3 | 865fc8a | Updated CONTRIBUTING.md |
| 8dae342 | ae3404c | Added Master ET model type |
| 7000400 | b1104ba | Updated 6502 emulation to depend on CPU type rather than model type |
| 5081c75 | 239f973 | Added Master ET ROMs |
| 3900169 | 07b6032 | Refactored ROM config file handling |
| da9d546 | 2346502 | Updated ROM config files |
| 4c4b150 | 5d37d8b | Renamed variables |
| 58d0791 | 2a0ecd0 | Updated User Port RTC |
| 85e1f78 | 0c55574 | Refactored Integra-B RTC implementation |
| 5cddbfc | cb8e9ea | Minor code simplifications |
| 4f572c6 | 4ce5f9e | Reverted Master CMOS RAM changes |
| 6ff008a | 36945d9 | Added MasterET checks |
| dfb0590 | 3a22b0c | Select CMOS registers by machine type in RTCInit() |
| 9497588 | e648ae2 | Fixed MachineType check |
| 83af3fa | 3db8326 | Added support for binary mode when accsesing the Master RTC |
| 9caeb16 | e9d8c62 | Fixed writes to RTC CMOS |
| fb84fb2 | 2b107ee | Fixed 146818 RTC/CMOS emulation |
| a46e6bd | bbc02a7 | Refactored 6502 CPU flag macros |
| af77ff4 | 2bf22da | Removed redundant OldPC variable |
| b5f9ba6 | df4366a | Updated CHANGES.md |
| 963ead8 | 197f0e9 | Removed unused 6502 trace code |
| 0bff0da | 5f41e63 | Updated documentation to include Master ET |
| a7cf78d | 9080db0 | Fixed compiler warnings |
| 512a7cb | e6f04c7 | Fixed installer script |
| 92b101d | d000ca5 | Fixed 6502 opcode ISC zp |
| ffaa87a | 1a39bc5 | Use unsigned char type for 6502 registers |
| ed50c63 | 6329a74 | Refactored 6502 opcodes |
| 28990f9 | 6329a74 | Refactored 6502 opcodes |
| f64644a | 6329a74 | Refactored 6502 opcodes |
| ae9624b | 6329a74 | Refactored 6502 opcodes |
| fe9d0e9 | 6329a74 | Refactored 6502 opcodes |
| 2f82f75 | 6329a74 | Refactored 6502 opcodes |
| f9749a9 | 6329a74 | Refactored 6502 opcodes |
| ee8ec80 | 6329a74 | Refactored 6502 opcodes |
| 3100c42 | 9080db0 | Fixed compiler warnings |
| bb8dc22 | 6329a74 | Refactored 6502 opcodes |
| 9fd4ccf | 6329a74 | Refactored 6502 opcodes |
| ba37892 | 6329a74 | Refactored 6502 opcodes |
| 1958cbc | 6329a74 | Refactored 6502 opcodes |
| 5dcccb7 | 9080db0 | Fixed compiler warnings |
| b26f94b | 6329a74 | Refactored 6502 opcodes |
| 58eec80 | 6329a74 | Refactored 6502 opcodes |
| bd35976 | 6329a74 | Refactored 6502 opcodes |
| 11dbe0f | 6329a74 | Refactored 6502 opcodes |
| 3d331d2 | 985aa11 | Refactored CPU opcodes |
| dde5b52 | 6329a74 | Refactored 6502 opcodes |
| e47a942 | 6329a74 | Refactored 6502 opcodes |
| 9e4f093 | 6329a74 | Refactored 6502 opcodes |
| d6a2cf6 | 6329a74 | Refactored 6502 opcodes |
| 862c9c9 | 6329a74 | Refactored 6502 opcodes |
| 10f2491 | 6329a74 | Refactored 6502 opcodes |
| b332186 | 6329a74 | Refactored 6502 opcodes |
| 7853869 | 6329a74 | Refactored 6502 opcodes |
| bf447dd | 6329a74 | Refactored 6502 opcodes |
| e0b908c | 6329a74 | Refactored 6502 opcodes |
| 65838b0 | 6329a74 | Refactored 6502 opcodes |
| d341716 | 6329a74 | Refactored 6502 opcodes |
| af7ebac | 6ef6fb3 | Refactored 6502 branch opcodes |
| 487700b | 6329a74 | Refactored 6502 opcodes |
| 112b6d4 | 6329a74 | Refactored 6502 opcodes |
| 44c3587 | 6329a74 | Refactored 6502 opcodes |
| d2b520d | 6329a74 | Refactored 6502 opcodes |
| 62ea7f2 | 6329a74 | Refactored 6502 opcodes |
| 71a635b | 6329a74 | Refactored 6502 opcodes |
| b68794e | 6329a74 | Refactored 6502 opcodes |
| fba66c1 | 6329a74 | Refactored 6502 opcodes |
| 765f24a | 6329a74 | Refactored 6502 opcodes |
| 0c82e0c | 6329a74 | Refactored 6502 opcodes |
| f6c6e1b | 6329a74 | Refactored 6502 opcodes |
| 125dbf8 | 6329a74 | Refactored 6502 opcodes |
| c08e9e3 | 9080db0 | Fixed compiler warnings |
| 852dc17 | c7ec6c3 | Fixed compiler warnings in Master512CoPro.cpp |
| 3f9913f | 9080db0 | Fixed compiler warnings |
| b555e36 | 9080db0 | Fixed compiler warnings |

</details>

### Dropped or skipped, with reasons

- **0b23a94**: dropped on Mark's decision; upstream's Econet rewrite replaces it. (Its idea was merged upstream as 36bc684 in any case.)
- **6fea847**: dropped on Mark's decision; Chris declined it and chose his own defaults (below).
- **a23aec2**: dropped; upstream's paste buffer is now dynamic (below).
- **c56d5e1 / d0b211b**: superseded. Upstream `UserPortRTC.cpp` (2a0ecd0, b880d87, 2539453, 5265d87) always reads the counters (registers 0, 2, 4, 6: month, date, hour, minute) from the PC clock, which is what d0b211b ended up with. It stores the odd "time/alarm" registers (1, 3, 5, 7) in `UserPortRTCRegisters`, persisted through Preferences (`CFG_USER_PORT_RTC_REGISTERS`, zeroed if absent), and it now also emulates the SAF3019P write wrap-around. The only part of c56d5e1 not upstream is this fallback: *if register 1 (the L3 FS year) is 0, return `BCD(year - 1981)`*. It is not ported because it is unsound: for 2026, year - 1981 = 45 gives 0x45, but upstream's own comment and write path say register 1 can only hold 0–19 (`Data % 20`), so the value could never come from real hardware. See the open questions.
- **9e28783, f958f89**: superseded, as in the table.
- Comment-only wording in c56d5e1 ("Day counter" for "Date counter", "alarm register"): not carried; upstream's comments now say "Date counter" and "(alarm)".

### Master 128 / Master ET CMOS defaults now upstream

`Src/Rtc.cpp`: `CMOSDefault_Master128[]` / `CMOSDefault_MasterET[]` (bytes 14–30). These are applied by `RTCSetCMOSDefaults()` only when the Preferences file has no saved CMOS for that model (`BeebWin::LoadCMOSPreferences`). Last changed by Chris in 0ec58cd (Jun 2024) and 74f9bad (Sep 2025, "no ROMs unplugged").

| Byte | Meaning | Master 128 | Master ET | 6fea847 proposed (M128 / ET) |
|---|---|---|---|---|
| 14 | Econet station | 0x01 (1) | 0x01 | same |
| 15/16 | File server | 254.0 | 254.0 | same |
| 17/18 | Printer server | 0xEA (234).0 | 0xEA.0 | 0xEB (235) |
| 19 | Lang / FS ROM | 0xC9: lang 12, FS 9 | 0xDE: lang 13, FS 14 | 0xCD / 0xDE |
| 20/21 | ROMs inserted | 0xFF 0xFF (all) | 0xFF 0xFF | 0xFF 0xFE / 0xFF 0xFF |
| 22 | EDIT | 0x32 | 0x32 | same |
| 24 | Mode / TV | 0x07: MODE 7, TV 0,0 | 0x07 | 0x17 |
| 25 | Floppy / boot / caps | 0xC0 | 0xC0 | 0xE0 / 0x20 |
| 26/27 | Repeat delay / rate | 30 / 8 cs | 30 / 8 | same |
| 28 | Printer ignore | 0 | 0 | same |
| 29 | 0x31: parallel printer, 1200 baud, Tube on | 0x31 | 0x31 | 0x0D / 0x01 |
| 30 | 0xA2: 8N1, no boot, scroll, ext Tube, loud | 0xA2 | 0xA2 | 0x80 / 0x08 |
| 31 | — | not set | not set | 0x42 |

In words: both machines default to station 1, file server 254, printer server 234 (see the open question below), MODE 7, all ROMs inserted, Tube on and no boot. The Master 128 defaults to language ROM 12 and filing system ROM 9; the Master ET defaults to language ROM 13 and filing system ROM 14.

### Clipboard recommendation (a23aec2): drop

- a23aec2 (7 Jun 2024) raised `static const int ClipboardBufferSize` from 32768 to 65536 for a fixed `char m_ClipboardBuffer[]` array.
- One day earlier, Chris committed 5be8d07 "Use vector to store text pasted from the clipboard" (6 Jun 2024). Upstream now has `std::vector<char> m_ClipboardBuffer` with `size_t` length and index (`BeebWin.h`). `BeebWin::OnPaste()` (`BeebWinIo.cpp`) does `m_ClipboardBuffer.resize(strlen(pData) + 1)` for the whole CF_TEXT clipboard, so there is **no fixed size or cap**; the limit is available memory.
- The constant a23aec2 changes no longer exists, so the patch would not even apply. Long HiBASIC pastes beyond 32 KB already work upstream; the bottleneck is now only the speed at which the OS reads the buffer through `ClipboardREMVHandler` (`6502core.cpp`), which also converts £ to ` and CR/LF to CR.
- Not committed.

### archive/localmaster (790518e) comparison

790518e "Added new models and changed the CPU instructions to be CPU specific instead of computer model specific" is a single commit on 785b63f, and an earlier squash of master-et's 8f95a37 + 7fda01b:
- **Model.h**: identical to 7fda01b's apart from comment text ("BBC Master ET" vs "Master ET"). Enum: B, IntegraB, BPlus, Master128, MasterET, MasterCompact, FileStoreE01, FileStoreE01S, Last; `enum class CPU : unsigned char`.
- **Model.cpp**: its `szModel[]` lacks the "BBC Master ET" and "BBC Master Compact" entries that 8f95a37 has, so `GetModelName()` would return the wrong name or read past the array for E01/E01S. This is a bug, already fixed in 8f95a37.
- **6502core.cpp**: four places still test `MachineType == Model::Master128` where 7fda01b tests `CPUType`. 7fda01b itself had two bugs (it inverted the ISC zp 0xE7 test, and `|| Model::MasterCompact` is always true; the latter was fixed by 029daa3). Upstream b1104ba plus d000ca5 "Fixed 6502 opcode ISC zp" is CPU-type based throughout.
- **About the brief's 7000400**: it is not an upstream hash. It is master-et's pre-rebase copy of Chris's "Updated 6502 emulation to depend on CPU type rather than model type"; the upstream commit in dccbcc8 is **b1104ba**.
- **Not upstream**: only the `MasterCompact`, `FileStoreE01`, `FileStoreE01S` and `Last` enum values, and `CPU : unsigned char`. MasterCompact was never implemented; FileStore is handled on `filestore-codeberg`. **Nothing to port.**

### Conflicts and resolutions

None arose, because nothing was cherry-picked. Every candidate was checked against the current code instead of being applied.

### Open questions for Mark

1. **Printer server default**: upstream's `CMOSDefault_*` byte 17 is 0xEA = **234**, but its comment says "Printer server number: 235". 6fea847 used 0xEB (235). Should Chris be asked to fix either the value or the comment?
2. **L3 file-server year (User Port RTC register 1)**: upstream defaults it to 0 until the L3 server writes it, and keeps it in Preferences afterwards. Do you still want an automatic year when it is 0? If so, it would have to fit 0–19 (for example `(year - 1981) % 20`), not `BCD(year - 1981)` as in c56d5e1.
3. Do you agree with upstream's Master ET language/FS slots (13/14) and "all ROMs inserted" for the Master 128? If so, 6fea847 can be forgotten.
4. Should `master-et-codeberg` be kept at all? It is identical to the base, so `master-et` could simply be deprecated with the others.

## keyboard-fix-codeberg

Experimental branch, not merged into anything. Base: `codeberg-master` dccbcc8 (Codeberg itself was not reachable when the branches were built).

### Commit mapping

| Old | New | Notes |
|---|---|---|
| aff61a2 "Keyboard: reconcile latched BBC key states with the physical keyboard" (on e605636) | 891955b | Cherry-picked. Author and date kept (mark-usher). A note was added to the message saying the original text describes an earlier version (see below). |

### Conflicts and resolutions

None. Upstream has not touched `Src/BeebWin.cpp`, `Src/BeebWin.h`, `Src/SysVia.cpp` or `Src/KeyMap.cpp` in the 20 commits between e605636 and dccbcc8, so the commit applies unchanged.

**The commit message does not match the code.** It says the change "re-checks recorded key states against GetAsyncKeyState() ... skipping scripted keyboard commands and shift-boot". The committed code:
- does **not** call GetAsyncKeyState (it only appears in comments);
- does **not** skip scripted keyboard commands;
- releases keys purely on timers.

The comment added in `OnSetFocus()` (`Src/BeebWin.cpp:5335-5342`) is stale in the same way: it says ResyncKeyboardState "will re-derive m_ShiftPressed from GetAsyncKeyState(VK_SHIFT)", and nothing does. I left the code as it is and recorded the mismatch in the commit message.

### Review

**Problem addressed.** If Windows never delivers a WM_KEYUP (a Bluetooth HID dropout, or a focus-change race), `m_vkeyPressed[]` and the BBC matrix (`SysViaKbdState`) keep the key down, so the BBC sees it held or auto-repeating until focus changes.

**What the code does.**
1. `ResetKeyboardState()` (`BeebWin.cpp:2955`) clears `m_ShiftPressed`, `m_vkeyPressed[][][]` and two new timestamp arrays. The constructor calls it, and so does `OnSetFocus(false)` after `BeebReleaseAllKeys()`.
2. `TranslateKey()` key-down path (`:2867-2882`): it stamps `m_vkeyPressedTick[vkey]` on every WM_KEYDOWN, including auto-repeats. It stamps `m_vkeyPressedStartTick[vkey]` only on the first press.
3. `ResyncKeyboardState()` (`:3013`) is called at the top of `StartOfFrame()`, i.e. once per emulated frame (50 Hz at 1x speed). It force-releases, through `TranslateKey(vkey, true, ...)`, any tracked vkey that has either:
   - had no WM_KEYDOWN for more than 600 ms, or
   - been held for more than 4 s in total.
4. The `KeyD`/`KeyU` DebugTrace lines in WndProc are re-enabled. They compile to nothing in release builds (`DebugTrace.h:26`).

**Interaction with upstream.**
- *Key mapping (logical, physical, user kmaps, A/S, F-key options):* unaffected. The resync works on the VK and replays the recorded row and column on release, so it works the same for every map and layout.
- *Shift tracking:* WndProc only ever sees VK_SHIFT (16), never 160 or 161, so the comment about 160/161 is inaccurate. Forcing a release of vkey 16 clears `m_ShiftPressed` (`:2833`). Upstream behaviour is unchanged: left and right Shift share one record, so releasing one releases BBC Shift while the other is still held.
- *Shift-boot / autoboot (`:4151`, `:6041`):* these use `BeebKeyDown(0,0)` directly and are never recorded in `m_vkeyPressed`, so the resync never touches them. `OnSetFocus(false)` already called `BeebReleaseAllKeys()` upstream, which drops a shift-boot Shift if focus is lost during boot. That behaviour is unchanged.
- *Scripted keyboard commands (`-KbdCmd`, `HandleKeyboardTimer()` `:6402`):* these **do** go through `TranslateKey`, so the resync affects them (see bugs 3 and 4).
- *Paste:* goes through OSRDCH interception (`6502core.cpp:1395`), not the matrix, so it is unaffected.
- *Joystick, AMX mouse, User Port breakout box:* none of these call TranslateKey or BeebKeyDown, so they are unaffected.
- *Focus changes:* resetting `m_vkeyPressed` and `m_ShiftPressed` on WM_KILLFOCUS is a real improvement on its own. Upstream clears the BBC matrix but leaves `m_ShiftPressed` set if Shift's key-up went to another window (Alt+Tab while holding Shift). That leaves the shifted column of the translation table in use, which looks like a stuck Shift until Shift is pressed again.

**Risks.**
- *Cost:* negligible. Once per emulated frame it scans 256 entries with one `GetTickCount64()`, and there is no GetAsyncKeyState polling. At high emulation speeds it simply runs more often.
- *Wall clock vs emulated time:* the timeouts use real time, but the scan runs per emulated frame. After a pause, a debugger break, or a slow-speed run, any recorded key is released on the first frame after resuming. That matters little for real keys but affects scripts (bug 4).
- *Non-UK layouts / AltGr:* AltGr sends a synthetic VK_CONTROL key-down that never auto-repeats, so it is released after 600 ms. That is harmless, arguably helpful. Dead keys still deliver WM_KEYDOWN. No layout-specific breakage found.
- *Alt / menu keys:* only F10 and VK_CONTROL reach TranslateKey from WM_SYSKEYDOWN, so there is no new interaction.

**Bugs found.** The first three were confirmed by compiling TranslateKey/ResetKeyboardState/ResyncKeyboardState unchanged against stubs and simulating key timing.
1. **Two keys held together: the first one is dropped (serious).** Windows auto-repeats only the most recently pressed key. Hold Z, press X and hold both: Z stops getting WM_KEYDOWN and is force-released 600 ms later while still physically held (simulated: at 2 s, Z released, X held). This breaks:
   - games: hold a direction and fire, rotate and thrust;
   - Shift or Ctrl held while typing: Shift stops repeating once a letter is pressed, so after a 600 ms pause Shift is dropped and the next letters come out unshifted;
   - Ctrl+Break if Break is held for more than 600 ms after Ctrl.
2. **Hard 4 s ceiling releases every long hold.** Holding one key, with auto-repeat arriving normally, still forces a release at 4 s. The next repeat re-presses it, and the cycle repeats every 4 s (simulated: 2 releases in 10 s). The BBC sees a key-up/key-down blip: it restarts MOS auto-repeat (a held cursor key stutters every 4 s), and games that poll the matrix see the key let go. A forced release of Break (F12) or another special key (row < 0) also loses the action, because WndProc acts on the key-up return value and the forced release discards it.
3. **Initial repeat delay longer than 600 ms.** The Windows keyboard "Repeat delay" setting gives 250/500/750/1000 ms. At the two longer settings, or with FilterKeys, every held key is released at 600 ms and pressed again when the first repeat arrives, so one long press types two characters (simulated with a 1 s delay: 1 spurious release). At the default 500 ms the margin is only about 100 ms.
4. **Scripted keyboard commands.** `\s` / `\c` (hold Shift/Ctrl) in a `-KbdCmd` string is held by the emulator with no Windows auto-repeat behind it, so it is dropped after 600 ms of real time. A `\dNNNN` delay of 600 ms or more releases the current key early. The commit message claims these are skipped; they are not.
5. *Minor:* stale comments (the GetAsyncKeyState references in `OnSetFocus`, the 160/161 Shift note). The `KeyD` DebugTrace passes WPARAM/LPARAM (64-bit on x64) to `%d`/`%X`. That is debug-only and was already present upstream as a commented-out line.

**Recommendation: do not merge as is.** The timeout heuristic cannot tell "key-up lost" apart from "key held but no longer the most recent key" (Windows behaviour), so it introduces worse bugs (1–3) than the one it fixes. Keep only the low-risk part:
- `ResetKeyboardState()` called from `OnSetFocus(false)` (and the constructor refactor). It fixes the stale-Shift-after-Alt+Tab case at no risk.

If a lost-key-up safety net is still wanted, possible better designs:
- (a) Handle `WM_INPUT` raw input, or check `GetKeyState`/`GetAsyncKeyState` only for keys whose own timer has expired, and skip any vkey the keyboard command script currently holds (`m_KbdCmdPress && vkey == m_KbdCmdKey`, plus `\s`/`\c` state).
- (b) Drop the 4 s ceiling entirely.
- (c) Use one timeout comfortably above the user's real repeat delay (`SystemParametersInfo(SPI_GETKEYBOARDDELAY)`), applied only to the most recently pressed key.

Before building a mechanism, it is worth first showing that the lost key-up happens with a wired keyboard at all.

**Static checks.** The whole of BeebWin.cpp passes a MinGW-w64 `-fsyntax-only` check with the project defines. The three changed functions were also extracted unchanged into a stubbed harness (`g++ -std=c++17 -Wall`), which is where the bugs above were reproduced. There are no stale identifiers or old filenames. `GetTickCount64` and `ULONGLONG` need Vista or later, which the existing VS project already targets. No new files, so the .vcxproj is untouched. **A Visual Studio build on Windows is still needed.**

### Manual test plan (Windows, Debug build so DebugTrace output shows in VS Output or DebugView)

1. **Baseline typing:** BBC B, BASIC. Type `PRINT "HELLO"` with Shift for the quotes and capitals. Expect correct characters and no `ResyncKeyboardState: forcing release` trace lines.
2. **Shift held while typing slowly (bug 1):** hold Left Shift, type `A`, wait 1 s, type `B`, release Shift. Expect `AB`. With this branch, expect `Ab` and a forced release of vkey 16.
3. **Two keys held (bug 1):** in BASIC run `10 IF INKEY-98 PRINT "Z"; : IF INKEY-67 PRINT "X"; : GOTO 10` (Z = -98, X = -67). Hold Z, then hold X as well, for 3 s. Expect a continuous `ZXZX...`. With this branch, Z stops after about 0.6 s.
4. **Long hold (bug 2):** same program, hold Z alone for 10 s. Expect an unbroken stream of Z. Then, at the BASIC prompt, hold Cursor Right for 10 s and watch for the cursor pausing every 4 s.
5. **Repeat delay (bug 3):** Control Panel > Keyboard > Repeat delay set to Long. In BASIC, hold `A` for about 0.8 s and release. Expect auto-repeat behaviour the same as on a real BBC, with no doubled first character. Reset the setting afterwards.
6. **Alt+Tab with Shift (the improvement):** hold Shift, Alt+Tab to Notepad, release Shift there, Alt+Tab back, type `a`. Expect lowercase `a`. On upstream dccbcc8, check whether it comes out shifted, to confirm the fix is worth keeping.
7. **Keyboard command script (bug 4):** run `BeebEm.exe -KbdCmd "\d1000\sa\Sb"`. Expect `Ab`.
8. **Shift-boot:** Ctrl+Shift+F12 / Run Disc with a `!BOOT` disc. Expect it to boot, same as upstream.
9. **Lost key-up (the original problem):** with the Bluetooth keyboard that showed the fault, type quickly for a few minutes. Note any stuck key and whether the trace shows a forced release.

### Open questions for Mark

- Was the stuck-key fault seen only on a Bluetooth keyboard? If so, do you want any heuristic at all, or just the focus-loss reset (the low-risk part)?
- Do you want me (or a later session) to cut the branch down to the `ResetKeyboardState()` / focus-loss part only, as a candidate for upstream?
- The message refers to an earlier GetAsyncKeyState version. Does that version survive anywhere, and was its "up during a real hold" finding taken with Remote Desktop or a VM in the path? Both are known to make GetAsyncKeyState unreliable.

## Next steps for Mark

1. Build the Release configuration as well (Debug x64 builds warning-free); build master-et-codeberg and keyboard-fix-codeberg.
2. Run the FileStore test plan above, and the keyboard test plan if you still want that change.
3. Answer the remaining open questions (Master ET 1-4; keyboard 1-3). The FileStore questions are all answered.
4. Only then ask Chris to delete `filestore`, `filestore-2` and `LocalMaster` on Codeberg (and stardot), as in Part A step 5.

## Change log

| Version | Date | Change |
|---|---|---|
| 1.0 | 19 Oct 2026 | First version, written while building the branches |
| 1.7 | 20 Oct 2026 | FileStore station from CMOS with session-only -EcoStn override; build fixes for Chris's code |
| 1.8 | 20 Oct 2026 | Fixed gateway keep-alive with a fixed GATEWAY; log failed station binds; note on old numeric MachineType |
| 1.10 | 20 Oct 2026 | New branch scsi-pi1mhz: Pi1MHz BeebSCSI drives, controllers, mode pages and .cfg descriptors |
| 1.9 | 20 Oct 2026 | Real Econet test set-up (net 7, bridge gateway); stations heard through the gateway shown in the Econet Network dialog; Codeberg PRs; history rewrite to mark-usher authorship |
| 1.6 | 20 Oct 2026 | filestore-econet: FileStore station number from its CMOS with AUTOCONFIGURE, as the Master |
| 1.5 | 20 Oct 2026 | Codeberg master unchanged (dccbcc8); new branch filestore-econet merging econet-reworking-2 |
| 1.4 | 20 Oct 2026 | Clock option generalised: Hardware > RTC Clock Offset for all models, `RTCClockOffset` in the preferences file |
| 1.3 | 20 Oct 2026 | FileStore questions 1, 3, 5 and 6 answered; 146818 timing emulation and the FileStore clock option |
| 1.2 | 20 Oct 2026 | FileStore question 2 (Rockwell instructions) resolved: not used by the FileStore source |
| 1.1 | 20 Oct 2026 | Added "Changes after the port" and "Build findings"; front panel, behaviour changes, open questions (4 and 7 resolved), test plan and next steps updated for the work up to 1a4c98b |
