Pocket Crystal League — Nintendo Switch port (GameMaker wrapper)
================================================================
 
This is a native wrapper / loader that runs the original ARM64 Android build of Pocket Crystal League v3.0.2 on Switch homebrew. It contains no game code and no game data — it loads the game's own GameMaker runner and recreates, natively, the thin Android/JNI layer the runner expects. The icon is the game's own splash art.
 
Install & run
-------------
 
```
sdmc:/switch/pcl_nx
├── pcl_nx.nro
├── assets          <- your APK
├── libyoyo.so      <- your APK
└── game.apk        <- your APK (any name ending in .apk works)
```
 
 Launch via title override (hold **R** while starting an installed game.
 
Controls
--------
 
| Input | Action |
| --- | --- |
| Touchscreen | Tap (handheld) |
| Left stick | Move the cursor |
| **A** / **ZR** | Left click |
| **B** / **ZL** | Right click (inspect a card) |
| Right stick / **L** / **R** | Scroll |
| D-pad | Arrow keys (page between screens) |
| **X** | Space (end turn) |
| **Y** | Tab (sort hand) |
| **+** | Esc |
| **–** | Software keyboard (name entry) |
| L3 / R3 | Recenter the cursor / middle click |
 
A USB mouse works in both modes. Every button can be remapped in `config.txt`.
 
Settings
--------
 
`config.txt` is written next to the `.nro` on first launch, with the options documented inline:
 
```
resolution   0       # 0 = 720p handheld / 1080p docked, or 720 / 1080
scale        fit     # fit, sharp (even pixels) or integer (black border)
vsync        1       # 1 = display paces the game; 0 = the runner does
cursor_speed 12.0    # left-stick speed
rstick       wheel   # wheel, arrows or none
btn_a        lclick  # btn_* : lclick rclick mclick wheel_up wheel_down
                     #         keyboard recenter gyro none, or key:<name>
debug_log    0       # 1 writes debug.log next to the .nro
```
 
Building
--------
 
Requires devkitPro with the `switch-dev` group plus these portlibs:
 
```
pacman -S switch-dev
pacman -S switch-mesa switch-libdrm_nouveau switch-sdl2 switch-freetype \
          switch-harfbuzz switch-libpng switch-bzip2 switch-zlib
 
export DEVKITPRO=/opt/devkitpro
make                        # -> pcl_nx.nro
```
 
Credits
-------
 
The loader/shim infrastructure (`so_util`, `libc_shim`, `jni_fake`, `opensles`, `nx_pointer`) derives from the open-source Switch `.so`-loader lineage — Andy Nguyen, fgsfds and ChanseyIsTheBest, building on TheOfficialFloW's Vita/Switch loader tradition — reaching this project via the POINPY port, with frame-pacing notes from Metroid Prime Origins. All MIT-licensed. Thanks to everyone in that lineage for making this approach possible.
 
Pocket Crystal League belongs to its creator. This is an unofficial fan project, not affiliated with or endorsed by them, Nintendo, or YoYo Games.
