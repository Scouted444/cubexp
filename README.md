# CubeXP

A Windows XP-style desktop "OS" for the Nintendo GameCube. It runs on real hardware through Swiss and in the Dolphin emulator.

![screens](docs/screens.png)

## What's inside (v1.2)

- **Real Linux**: the Linux Terminal boots an actual Linux 6.8 kernel with BusyBox, on an emulated RISC-V CPU
- **Sharper look**: every icon redrawn with shading, gloss and a soft drop shadow, rendered 4x and scaled down; a crisp Tahoma-style font; soft window and menu shadows
- **Smooth motion**: windows fade in and shrink away when closed, minimize/maximize animate, the Start menu slides up, screens crossfade between boot, log-in and desktop, and menus, tooltips and balloons fade
- **Real file system**: Local Disk (C:) with Desktop, Documents, Pictures, Music, Programs, System and Recycle Bin, all saved to the SD card
- **Desktop**: files and shortcuts in `/Desktop` show up as icons; right-click to make folders, documents and sticky notes
- **Taskbar**: quick launch (Show Desktop, Explorer, Terminal), tooltips, and a **Z window switcher** like Alt+Tab
- **Start menu**: My Documents, My Pictures and My Music links, plus a two-column All Programs list

### Programs (23)

| Program | What it does |
|---|---|
| **Linux Terminal** *(new in 1.2)* | Real Linux (see below) |
| **Terminal** | CubeXP's own shell, wired into your files (see below) |
| **Windows Explorer** *(new)* | Browse, create, rename, cut/copy/paste and delete files; icon and details views |
| **Solitaire** *(new)* | Klondike with drag and drop, scoring, and the bouncing-card win animation |
| **Blocks** *(new)* | Falling-block puzzle with a ghost piece, levels and high score |
| **Music Maker** *(new)* | Piano and 16-step sequencer with 3 sounds; saves `.song` files |
| **Picture Viewer** *(new)* | View BMPs, flip through a folder, set as background, edit in Paint |
| **Sticky Notes** *(new)* | Coloured notes that come back every time you log on |
| Notepad | Open/Save dialogs, status bar, copy/paste |
| Paint | Saves and opens BMPs; Set as Background (kept across restarts) |
| Command Prompt | The same shell, in DOS style (`dir`, `copy`, `del`, `type`...) |
| Minesweeper, Snake, Pong, Calculator, Control Panel, Task Manager, My Computer, Recycle Bin (restore works), Clock, On-Screen Keyboard, Run, Help | |

### Linux Terminal

Opens a window running a **real Linux kernel** (6.8, no-MMU, RISC-V `rv32ima`) with a BusyBox userland, inside Charles Lohr's mini-rv32ima emulator. It has a VT100 terminal with colours, cursor movement and scrollback, so `ls`, `cat /proc/cpuinfo`, `vi`, `top`, `free` and shell scripts all work.

- Toolbar buttons for **Ctrl** (sticky: tap it, then a key), Ctrl+C, Ctrl+D, Tab, Esc, arrows, Paste and Reboot. Type with the On-Screen Keyboard.
- The first boot takes a while: every RISC-V instruction is emulated on the GameCube's 485 MHz CPU. The speed (MIPS) shows in the corner. Linux keeps running in the background at lower speed when the window isn't active.
- It's a separate computer: Linux has its own RAM disk and can't see CubeXP's files, and it resets when you close the window.
- It uses about 12 MB of RAM, so close big programs first if it says there isn't enough memory.

### CubeXP Terminal

About 70 commands, including `ls cd pwd mkdir rm cp mv cat touch tree find grep wc head tail sort uniq echo seq xxd stat du df`, plus `ps kill free uptime neofetch cowsay fortune matrix play theme wallpaper calc date alias history` and `shutdown`.

- Pipes and redirects: `ls | grep txt > list.txt`, `>>`, `<`
- Variables `$USER`, `set NAME=x`, `$(command)` substitution, `$?`
- Wildcards (`rm *.bak`), chaining with `;`, `&&` and `||`, and `for f in *.txt; do cat $f; done`
- `.sh` scripts (try `sh /Documents/hello.sh`) and `/System/autorun.sh`
- Tab completion, command history, scrollback, and tappable chips for controller users
- Any program name opens it: `snake`, `explorer`, `notepad todo.txt`

## Controls

| Button | Action |
|---|---|
| Control Stick | Move the pointer |
| A / B | Click / right-click |
| START | Start menu |
| X | On-Screen Keyboard |
| Z | Window switcher (press again to cycle) |
| Y | Show desktop (Enter while typing) |
| C-Stick | Scroll |
| D-Pad | Arrow keys and game controls |
| L / R | Backspace / Space while typing |

## Running it

**Dolphin:** File > Open > `cubexp.dol`. To let it save, add an SD card adapter in Config > GameCube (Slot A, Slot B or SP1).

**Real GameCube:** copy `cubexp.dol` to your SD card and launch it from Swiss.

## Building

Every push builds automatically with GitHub Actions. Download `cubexp.dol` from the **latest** release, or from the workflow run's artifacts.

To build locally, install devkitPro with `gamecube-dev`, then run `make`.

### PC test harness

`tools/host/build.sh` compiles the whole OS for Linux with a fake platform layer. It runs a script of button presses and saves screenshots, which lets you check the UI without an emulator:

```
./tools/host/build.sh
SCRIPT=myscript.txt ./tools/host/cubexp_host
```

Script commands: `wait N`, `warp X Y`, `tap A`, `hold A`, `release A`, `type text_with_underscores`, `enter`, `launch N`, `closeall`, `shot file.ppm`, `quit`.

## Credits

- **mini-rv32ima** RISC-V emulator by Charles Lohr (MIT licence): https://github.com/cnlohr/mini-rv32ima (`source/linux/`).
- The Linux kernel and BusyBox image is Charles Lohr's prebuilt `linux-6.8-rc1-rv32nommu-cnl-1` from https://github.com/cnlohr/mini-rv32ima-images. It's downloaded at build time by `tools/fetch_linux.sh` and is not stored in this repo. Linux and BusyBox are GPLv2; their sources and build configuration are linked from those repositories.

The UI fonts are generated by `tools/genfont.py` from Liberation Sans (SIL OFL) and DejaVu (Bitstream Vera license). CubeXP is a fan homebrew project and is not affiliated with Microsoft or Nintendo.
