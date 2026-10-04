#!/bin/sh
# Start NEOFAN.EXE under Wine once per Windows version and check that it
# runs, draws its window and writes a log file.
#
#   tests/wine_smoke.sh build/NEOFAN.EXE out_dir [drive_dir]
#
# drive_dir, if given, is mapped as drive L: (for example a mounted FAT32
# or NTFS image) and the log is written there.
# Needs: wine (32-bit), xvfb-run, ImageMagick "import".
set -u
EXE=$(readlink -f "$1")
OUT=$(readlink -f "$2")
DRIVE=${3:-}
mkdir -p "$OUT"
export WINEPREFIX=${WINEPREFIX:-$HOME/.wine-neofan}
export WINEARCH=win32 WINEDEBUG=-all
[ -d "$WINEPREFIX/drive_c" ] || wineboot -i >/dev/null 2>&1
C="$WINEPREFIX/drive_c"
LOGDIR='C:\NFSTEST\LOGS'
if [ -n "$DRIVE" ]; then
    rm -f "$WINEPREFIX/dosdevices/l:"
    ln -s "$(readlink -f "$DRIVE")" "$WINEPREFIX/dosdevices/l:"
    LOGDIR='L:\NEOFAN'
fi
STATUS=0
for VER in ${VERSIONS:-win98 winme win2k winxp}; do
    rm -rf "$C/NFSTEST"
    mkdir -p "$C/NFSTEST"
    cp "$EXE" "$C/NFSTEST/NEOFAN.EXE"
    [ -n "$DRIVE" ] && rm -rf "$DRIVE/NEOFAN"
    printf '[General]\r\nDetected=1\r\nTab=%s\r\n[Log]\r\nFile=%s\\NFS001.CSV\r\nEvery=1\r\nOnStart=1\r\n' "${TAB:-0}" "$LOGDIR" > "$C/NFSTEST/NEOFAN.INI"
    wine reg add 'HKCU\Software\Wine' /v Version /d "$VER" /f >/dev/null 2>&1
    wineserver -w
    for MODE in ${MODES:-demo hw}; do
        ARG=/demo
        [ "$MODE" = hw ] && ARG=
        if [ -n "$DRIVE" ]; then LD="$DRIVE/NEOFAN"; else LD="$C/NFSTEST/LOGS"; fi
        rm -rf "$LD"
        if [ "${ROLLTEST:-0}" = 1 ]; then
            # a log that is already 1 MB: the program must move on to NFS002.CSV
            mkdir -p "$LD"
            head -c 1048600 /dev/zero | tr '\0' 'x' > "$LD/NFS001.CSV"
        fi
        xvfb-run -a -s "-screen 0 1024x768x24" sh -c "
            cd '$C/NFSTEST' && wine NEOFAN.EXE $ARG >'$OUT/$VER-$MODE.wine.txt' 2>&1 &
            sleep 9
            import -window root '$OUT/$VER-$MODE.png' 2>/dev/null
            wine '$C/windows/system32/taskkill.exe' /im NEOFAN.EXE >/dev/null 2>&1
            sleep 2
            wineserver -k
        " >/dev/null 2>&1
        LOG="$LD/NFS001.CSV"
        [ "${ROLLTEST:-0}" = 1 ] && LOG="$LD/NFS002.CSV"
        if [ -f "$LOG" ]; then
            ROWS=$(($(wc -l < "$LOG") - 1))
            cp "$LOG" "$OUT/$VER-$MODE.csv"
            echo "ok    $VER $MODE: window shown, $(basename "$LOG") has header and $ROWS rows"
            [ "$ROWS" -ge 3 ] || { echo "FAIL  $VER $MODE: too few rows"; STATUS=1; }
        else
            echo "FAIL  $VER $MODE: no log file ($(tail -c 300 "$OUT/$VER-$MODE.wine.txt" | tr '\n' ' '))"
            STATUS=1
        fi
    done
done
exit $STATUS
