# glOSports-gta3 — GTA III, native, on the GA36-MB

Grand Theft Auto III through Rockstar's Android build (War Drum Studios,
2011): a loader (`loader/`) maps the game's own `libR1.so` (armeabi-v7a),
resolves its imports to goodluckOS's glibc, ARM's Mali GLES 2.0 driver (from
the folder's `glos/`, see `goodluckOS-mali-driver`), OpenAL Soft and SDL's pad,
answers its Java calls through a fake JNI, and runs it. The loader core is
carried over from `glOSports-gtasa`. Separate from `glOSports-re3`, which
runs the PC data on re3. There is no Vita loader of this build to take the
game's fixes from. The console stays stock goodluckOS.

Status: runs on the GA36-MB. Menus at 30 fps (the game's frame
limiter), gameplay about 25–30 fps; saves and loads work.

## Game files

From your own install of the Android game (package `com.rockstar.gta3`), into
`/home/player/ports/gta3/game/`:

| From | Version | What goes into `game/` |
|---|---|---|
| `main.5.com.rockstar.gta3.obb` | 5 | its contents (it is a zip), first |
| the `.apk` | version 1.6 (`libR1.so` of 2015-03-05, md5 `884abb8d…`) | the contents of `assets/` over the OBB's (its `TEXT/*.gxt` are newer), and `lib/armeabi-v7a/libR1.so` |

The OBB is in `/sdcard/Android/obb/com.rockstar.gta3/` on the phone. Files
named `…~HEAD` or `…~<hash>` in some unpacked APKs are leftovers of the tool
that unpacked it and aren't needed. The game opens everything loose from
`game/`: the loader answers no archive name, so the game's own ZIP layer is
never used, and its fallback to the APK (a Java helper) is switched off.

## The game folder on the console (`/home/player/ports/gta3/`)

| | |
|---|---|
| `gta3` | the loader (DT_RPATH `$ORIGIN/lib`) |
| `lib/` | OpenAL Soft 1.19 and its dependencies (sndio, bsd, md) |
| `alsoft.conf` | OpenAL reads it from the game folder by itself (ALSA) |
| `game/` | the game files above; saves land here too |
| `glos/` | the ARM driver set from `goodluckOS-mali-driver` |
| `start.sh` | pad mapping and settings, then `exec ./glos/glos-gpu run mali ./gta3` |

Launcher entry: `gta3.puppy` ("Grand Theft Auto III", with the re3 port's icon). Log: `/dev/shm/gta3.log`.

## Settings (`start.sh`)

| Variable | Default | |
|---|---|---|
| `GAME_STICK_SCALE`, `GAME_STICK_DEADZONE` | 1.5, 0.12 | stick shaping (radial dead zone after scaling) |
| `GAME_FPS_COUNTER` | 0 (1 in `start.sh`) | frames per second, top right |
| `GAME_AUDIO_RATE` | 44100 | OpenAL mixing rate (the game asks for 22050) |

## What the loader changes in the game (`loader/game.c`)

- **Files:** the game is told there is no archive, so it opens everything
  loose from `game/`; its fallback to the APK (a Java helper) is off.
- **Saves:** a save is written to `GTA3sf<n>.b.tmp` and renamed through Java;
  the loader does the rename.
- **Pad:** the `WarGamepad_*` calls answer from SDL's pad, reported as an
  Xbox 360 pad.
- **Button prompts:** help texts fill `~k~~ACTION~` from the action's PC
  joystick binding (the mobile build runs the pad through the PC joystick
  layer); the text for it (`FEC_JBO`, "Joy %d") is missing from the mobile
  `.gxt`, so the prompts were empty. They now name the GA36-MB's button
  (A, B, X, Y, LB, RB, LT, RT, BACK, ...). Look behind and target cycling
  are bound to joystick buttons the Xbox 360 handler never sets, so theirs
  stay empty.
- **Touch icons in help texts:** some texts write buttons as touch tokens
  (`Tap ::SUBMISSIONS:: to start`), drawn as touch-button icons. After the
  text loads (`CText::Load`, through its four calls) the tokens are
  replaced with the names of the pad buttons the game binds to those
  actions by default: FIRE, PUNCH and THROW B, SPRINT and ACCEL A, BRAKE X,
  DOOR Y, HANDBRAKE and LOCKON RB, ZOOMIN LT, ZOOMOUT RT, SUBMISSIONS L3,
  HORN R3, CAMERA BACK. The stats icons (kills, money, ...) stay.
- **HUD:** the HUD is laid out like the touch controls: the radar is drawn in
  the touchscreen's radar button, the clock, money, health, weapon and
  wanted level in its summary button, the help box in its tut button. The
  loader places them every frame: the radar with SA's `Adjustable.cfg`
  centre and radius (76, 370, 44 in 640×448), the stats block at SA's
  measured inset, the help box top left at the same insets. The zone and
  vehicle names, subtitles, pager and mission timers are drawn at fixed
  positions in `CHud::Draw` and stay where the game puts them. GTA III reads no HUD
  layout file of its own.
- **Camera:** Vice City's mobile build feeds the right stick into
  `Mobile_AdjustCam`, the offset a touch swipe gives; GTA III's on-foot
  camera already turns by it, but its `CCamera::CamControl` turns the stick
  into a first-person look. Its two stick reads there go to the loader,
  which feeds `Mobile_AdjustCam` with Vice City's numbers and answers 0. In
  a vehicle `CCam::Process` turned by it only during a touch swipe; that
  check is gone and the stick is fed every frame. `LookAroundUpDown`
  applies Invert Look once: off pushes the view the way the stick goes.
- **Lock-on:** the mobile `CPlayerPed::ProcessPlayerWeapon` looks for a
  target only while firing (auto-aim) and reads the target button only
  there, where holding it drops the lock, so RB alone did nothing. The
  game's one call goes through the loader: while RB is held with a weapon
  that can aim (not the first-person M16, sniper rifle, rocket launcher),
  it finds a target, points the gun at it and keeps the lock; firing then
  shoots at it; releasing RB clears it. A right-stick flick past half, LT
  or RT moves the lock to the next target on that side
  (`FindNextWeaponLockOnTarget`; none there keeps it). Both pick by GTA
  SA's rules (`EvaluateTarget`, `EvaluateNeighbouringTarget`,
  `FindTargetPriority`, `LOSBlockedBetweenPeds`): each candidate is scored off the player's
  facing and, when in front of the camera, off the camera's heading, the
  better counting; the score is 1 − 0.785 × the angle off over the fourth
  root of the distance, times 0.8 for attackers and 0.1 for the rest, and
  counts only in line of sight (buildings and objects). The next target is
  the nearest in angle from the camera on the asked side, within 50
  degrees. The game's own pre-check (in front of the player, 40 m), which
  SA doesn't have, is answered yes.
- **D-pad:** outside menus and pauses (`CTimer`'s user and code pause) the
  D-pad goes to the left stick instead of its buttons, so all four
  directions walk (down changed the camera) and left / right steer.
- **Startup:** the legal screens and "tap to start" count as shown
  (`shownLegalScreen`), so the game opens on the main menu after its videos.
- **Off:** the app-rating popup and vibration feedback (cloud saves are already off in this build).

The `timecyc.dat` of the mobile release is the PC one, byte for byte.

## Videos

The logos and the opening titles (`game/movies/Logo.mpg.m4v`,
`GTAtitles.mpg.m4v`, `GTAtitlesGER.mpg.m4v` in German) play at startup as
they come in the OBB (H.264 860x480, AAC), no conversion: 25 fps, no frame
dropped. A or Start skips. On Android a Java MediaPlayer shows them while
`MainGameTick` ticks without drawing and polls `OS_MovieIsPlaying`; here
`OS_MoviePlay` and `AND_IsMoviePlaying` / `AND_StopMovie` go to `loader/movie.c`
(the player of `glOSports-gtactw`: FFmpeg decoding on its own thread, a GL
context of its own on the game's surface, sound through the folder's OpenAL),
and each poll, on the game thread that holds the screen, shows the newest
picture. `OS_MovieStop` is a 4-byte branch to `AND_StopMovie`, too short for a
hook. Without `game/movies/` the game goes straight to the menu.

## Build

```
./build.sh      # loader/ -> build/gta3 (Docker, Debian bookworm armhf)
./package.sh    # -> out/ports/gta3/ and out/.local/share/applications/gta3.puppy
```

`build.sh` runs `tools/build-ffmpeg.sh` first when `build/ffmpeg/` is
missing: a static FFmpeg 6.1.2 (MP4 demuxer, H.264 and AAC decoders, NEON),
from ffmpeg.org's `ffmpeg-6.1.2.tar.xz` (sha256 `3b624649…`) unpacked in
`upstream/ffmpeg-6.1.2`.

Test tools are shared: `../goodluckOS-tools/testing-sync.sh gta3` puts them
on the console (`glos/testing/`), `testing-clean.sh gta3` takes them off,
`shot.sh gta3 file.png` takes a screenshot (see `goodluckOS-tools`).

## Licence

The port's own code is MIT (`LICENSE`); `package.sh` puts a copy into the game folder's
`licenses/`. Third-party code it carries keeps its own licence (below).

## Third-party components

Code carried over from the Vita loaders it names keeps their MIT notices;
those, FFmpeg (LGPL-2.1, linked statically for the videos), OpenAL Soft and
the other bundled libraries, and ARM's Mali driver are listed with their
licences in `licenses/`, which `package.sh` copies into the game folder. The
game itself is not included.
