# neoFanSpeed build. Needs the MinGW-w64 32-bit cross compiler:
#   apt-get install gcc-mingw-w64-i686 binutils-mingw-w64-i686
# The program links no C runtime and targets the Windows 4.0 subsystem,
# so the same NEOFAN.EXE runs on Windows 98, 98SE, ME, 2000 and XP.

PREFIX  ?= i686-w64-mingw32-
CC      := $(PREFIX)gcc
WINDRES := $(PREFIX)windres
OBJDUMP := $(PREFIX)objdump

# _WIN32_IE=0x0401 is the common controls level that ships with Windows 98,
# so the headers hide anything newer.
DEFS    := -DWINVER=0x0400 -D_WIN32_WINDOWS=0x0410 -D_WIN32_WINNT=0x0400 -D_WIN32_IE=0x0401 -DNOMINMAX
CFLAGS  := -std=gnu99 -Os -march=i486 -mtune=pentium -mno-mmx -mno-sse -mno-sse2 \
           -ffreestanding -fno-stack-protector -fno-asynchronous-unwind-tables -fno-ident \
           -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type -Wno-parentheses $(DEFS)
LDFLAGS := -nostdlib -nostartfiles -mwindows -s \
           -Wl,-e,_WinMainCRTStartup \
           -Wl,--subsystem,windows:4.0 \
           -Wl,--major-os-version,4 -Wl,--minor-os-version,0 \
           -Wl,--major-subsystem-version,4 -Wl,--minor-subsystem-version,0 \
           -Wl,--disable-dynamicbase -Wl,--disable-nxcompat -Wl,--image-base,0x400000
LIBS    := -lcomctl32 -lcomdlg32 -lshell32 -lgdi32 -luser32 -ladvapi32 -lkernel32 -lgcc

SRC     := src/ui.c src/app.c src/hw.c src/log.c src/draw.c src/mini.c src/rt.c
OBJ     := $(SRC:src/%.c=build/%.o) build/app.res.o
EXE     := build/NEOFAN.EXE

all: $(EXE)

build:
	mkdir -p build

build/%.o: src/%.c src/*.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/app.res.o: res/app.rc res/app.ico res/app.manifest src/resource.h | build
	$(WINDRES) $(DEFS) -I src -O coff -i $< -o $@

$(EXE): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

check: $(EXE)
	python3 tools/check_pe.py $(EXE)

clean:
	rm -rf build

.PHONY: all check clean
