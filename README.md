# neoFanSpeed

A small temperature and fan speed monitor for Windows 98, 98SE, ME, 2000
and XP. One program file, `NEOFAN.EXE`, runs on all of them.

![Sensors tab](docs/sensors.png)

## What it does

- **Sensors tab**: CPU diode, motherboard, a third board sensor and the hard
  disk (S.M.A.R.T.) temperature, with current, lowest, highest and average
  values, an alarm limit per sensor and a 60 second history graph.
- **Fans tab**: speed of up to three fans. When the sensor chip can drive a
  fan, you can pick Auto (BIOS), Manual (a fixed duty), Profile (Silent,
  Normal, Full speed) or Curve (four points with hysteresis). Fans the chip
  cannot drive are shown as read only, with the reason.
- **Fan safety**: a lowest duty floor, full speed on any alarm (with a
  Restore My Settings button) and hand back to the BIOS on exit.
- **Logging tab**: writes CSV or plain text every 1 to 60 seconds, picks the
  columns, starts a new file at 1 MB (`NFS001.CSV`, `NFS002.CSV`, ...).
- **System Info tab**: CPU name, core, clock and cache, graphics adapter,
  chipset, BIOS, sensor chip, voltages, Windows version, memory and the file
  system of the log drive. Copy to Clipboard and Save Report.
- **Mini View**, a tray icon that shows the temperature, a tray menu and,
  on ME, 2000 and XP, alarm balloons.

| Fans, curve mode | Logging | System Info | Mini View |
|---|---|---|---|
| ![](docs/fans-curve.png) | ![](docs/logging.png) | ![](docs/system-info.png) | ![](docs/mini-view.png) |

## What works on which Windows

| | 98 / 98SE / ME | 2000 / XP |
|---|---|---|
| CPU, graphics, chipset, BIOS, memory details | yes | yes |
| Board temperatures, fan speeds, voltages | yes, with a supported chip | no, see below |
| Fan control | yes, Winbond W83627HF and W83697HF | no |
| Hard disk temperature (S.M.A.R.T.) | yes, needs `SMARTVSD.VXD` | yes, run as administrator |
| Logging, Mini View, tray icon | yes | yes |
| Alarm balloons | ME only | yes |

**Why 2000 and XP get less.** The sensor chip sits on old ISA ports. Windows
98, 98SE and ME let a normal program read those ports. Windows 2000 and XP
block that and need a kernel driver, and neoFanSpeed does not ship one. On
those versions it says so in a banner, keeps the CPU, graphics and disk
readings and turns the fan controls off. Nothing is written to the hardware.

**Supported sensor chips** (found through the Super I/O ports 2Eh and 4Eh,
or at 290h on older boards):

- Winbond W83627HF, W83697HF: temperatures, fans, voltages, fan control on
  fan inputs 1 and 2.
- Winbond W83627THF, W83637HF, W83781D, W83782D, W83783S and ITE IT8705F,
  IT8712F: temperatures, fans and voltages, read only.
- Any other chip is named in a "not supported" banner with Save Chip Report.

Board makers wire sensors in their own way, so "CPU Diode" (chip
temperature 2) and "Motherboard" (temperature 1) may be swapped on some
boards. Names can be changed in `NEOFAN.INI` (`[Sensor1]` to `[Sensor5]`,
`[Fan1]` to `[Fan3]`, key `Name`).

Fan control writes the chip's PWM duty register. The value the BIOS left
there is saved first and put back on Auto (BIOS) and on exit, unless you
untick "Return fans to BIOS control on exit".

## Running it

A ready build is in [`build/NEOFAN.EXE`](build/NEOFAN.EXE). Copy it to any folder and start it. Settings go to `NEOFAN.INI`
in the same folder. The first start shows the hardware scan.

- `NEOFAN.EXE /demo` shows simulated readings, to try every screen on any PC.
- `NEOFAN.EXE /nohw` skips the sensor chip on 98 and ME.

The default log file is `C:\NEOFAN\NFS001.CSV`. Names stay in the 8.3 form,
so they work on FAT, FAT32 and NTFS, and a log never grows past 2 GB, under
the FAT32 4 GB file limit.

## Building

On Linux (Debian or Ubuntu):

```sh
sudo apt-get install gcc-mingw-w64-i686 binutils-mingw-w64-i686 make python3
make          # builds build/NEOFAN.EXE
make check    # checks the program will load on Windows 98
```

The program is plain C and the Win32 API. It links no C runtime, so its
only imports are `KERNEL32`, `USER32`, `GDI32`, `ADVAPI32`, `COMCTL32`,
`COMDLG32` and `SHELL32`, all as they ship with Windows 98. It is built for
the Windows 4.0 subsystem and for a 486 or better CPU (no CMOV, MMX or SSE).
Newer calls (balloons, XP themes, `EnumDisplayDevices`) are looked up at run
time and skipped when missing.

`tools/make_icon.py` rebuilds `res/app.ico` from the two SVG files, with
16 and 256 color images that 98 and ME can show.

## Tests

- `make check` runs `tools/check_pe.py`: PE header (subsystem and OS version
  4.0, alignment, no TLS), every imported function against
  `tools/win98_api.txt` (calls Windows 98 exports), and the code for
  instructions newer than the Pentium.
- `tests/wine_smoke.sh build/NEOFAN.EXE out/` starts the program under Wine
  set to report Windows 98, ME, 2000 and XP, in demo mode and with real
  hardware access, takes a screenshot, and checks that the log file has a
  header and rows. Add a third argument to log onto another drive, for
  example a mounted FAT32 or NTFS image. `ROLLTEST=1` starts with a full
  1 MB log and checks that `NFS002.CSV` is made.
- `tests/fat32_fuse.py` mounts a FAT32 image through a user space FAT driver,
  for machines where the kernel FAT driver is not available.

Wine is not real Windows. These tests show the program loads, draws and
logs with each version's settings. They cannot reach a real sensor chip, and
the final check is a run on a real 98, ME, 2000 or XP machine.
