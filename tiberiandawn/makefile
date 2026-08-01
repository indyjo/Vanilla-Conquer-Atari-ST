#
# Makefile for Tiberian Dawn DLL
# Customizable build configuration
# Configured for GNU cross-compiler toolchain (m68k-atari-mint)
#
# WARNING: This is a Windows-specific project that requires significant porting
# to work on m68k-atari-mint. The code includes Windows headers (windows.h)
# and uses Windows-specific APIs that don't exist on Atari/MiNT.
# This Makefile provides a starting point but the code will need porting.
#

#=============================================================================
# CONFIGURATION - Customize these variables
#=============================================================================

# Cross-compiler toolchain directory
CROSS_PREFIX = $(HOME)/opt/cross-mint/bin
CROSS_TARGET = m68k-atari-mint

# Build configuration: Debug or Release
CONFIG ?= Release
# Accept lowercase config values from command line.
ifeq ($(CONFIG),debug)
override CONFIG := Debug
endif
ifeq ($(CONFIG),release)
override CONFIG := Release
endif

# Compiler selection (uses cross-compiler from CROSS_PREFIX)
CXX = $(CROSS_PREFIX)/$(CROSS_TARGET)-g++
CC = $(CROSS_PREFIX)/$(CROSS_TARGET)-gcc
ASM = $(CROSS_PREFIX)/$(CROSS_TARGET)-as
LD = $(CROSS_PREFIX)/$(CROSS_TARGET)-ld
AR = $(CROSS_PREFIX)/$(CROSS_TARGET)-ar
# Resource compiler - windres may not be available for m68k
# If not available, resource files will be skipped in the build rule
RC = $(CROSS_PREFIX)/$(CROSS_TARGET)-windres

# Detect target platform from cross-compiler
# If using m68k-atari-mint, default to AtariST platform
# PLATFORM can be overridden on command line: make PLATFORM=Win32
ifeq ($(CROSS_TARGET),m68k-atari-mint)
PLATFORM ?= AtariST
PLATFORM_LIB = ATARILIB
else
PLATFORM ?= Win32
PLATFORM_LIB = WIN32LIB
endif

# Allow manual override
ifdef USE_ATARILIB
PLATFORM_LIB = ATARILIB
endif
ifdef USE_WIN32LIB
PLATFORM_LIB = WIN32LIB
endif

# Output directory
OUTDIR ?= ./bin/$(PLATFORM)
INTDIR ?= $(PLATFORM)/$(CONFIG)

# Project name
PROJECT_NAME = TiberianDawn

# Target name (config specific - same convention for all platforms)
ifeq ($(CONFIG),Debug)
TARGET_NAME = $(PROJECT_NAME)d
else
TARGET_NAME = $(PROJECT_NAME)
endif

# Platform-specific target name override for Atari ST
ifeq ($(PLATFORM_LIB),ATARILIB)
ifeq ($(CONFIG),Debug)
TARGET_NAME = cncd
else
TARGET_NAME = cnc
endif
endif

#=============================================================================
# COMPILER FLAGS
#=============================================================================

# Common preprocessor definitions
DEFINES = TRUE_FALSE_DEFINED

# Platform-specific definitions
ifeq ($(PLATFORM_LIB),ATARILIB)
DEFINES += POSIX BIG_ENDIAN ATARI_ST
# PROFILE_FRAME_BAR defines ST_FRAME_BAR_PROFILE (Hz200 frame/render/logic/theme bars in tab strip)
PROFILE_FRAME_BAR ?= 1
ifeq ($(PROFILE_FRAME_BAR),1)
DEFINES += ST_FRAME_BAR_PROFILE
endif
# PROFILE_BORDER: audio subsystem pen-0 border timing (ST_BORDER_PROFILE; not the frame bar)
PROFILE_BORDER ?= 0
ifeq ($(PROFILE_BORDER),1)
DEFINES += ST_BORDER_PROFILE
endif
# SEPARATE_DEBUG_SCREEN: dedicated game planar buffer + Ctrl+F10 TOS console toggle
SEPARATE_DEBUG_SCREEN ?= 0
ifeq ($(SEPARATE_DEBUG_SCREEN),1)
DEFINES += ST_SEPARATE_DEBUG_SCREEN
endif
# CHEAT_KEYS: full cheat keys and special command-line options (-DCHEAT_KEYS)
# Default on for Debug, off for Release; override: make CHEAT_KEYS=1
ifeq ($(CONFIG),Debug)
CHEAT_KEYS ?= 1
else
CHEAT_KEYS ?= 0
endif
ifeq ($(CHEAT_KEYS),1)
DEFINES += CHEAT_KEYS
endif
else
DEFINES += WIN32 _WINDOWS _USRDLL TIBERIANDAWN_EXPORTS
endif

# Include directories (platform-specific + common)
# Put platform lib first so platform-specific headers take precedence over root headers
INCLUDES = -I./$(PLATFORM_LIB) -I. -I./COMMONLIB -I.. -I../common
ifeq ($(PLATFORM_LIB),ATARILIB)
INCLUDES += -I./$(PLATFORM_LIB)/stvq
endif

# Some m68k-atari-mint installs omit libstdc++ from the primary sys-root; headers may live under sys-root-old.
ifeq ($(PLATFORM_LIB),ATARILIB)
MINT_CROSS_TOP := $(shell dirname "$(CROSS_PREFIX)")
LIBSTDCPP_VER := $(shell "$(CXX)" -dumpversion 2>/dev/null || echo 14)
MINT_LIBSTDCPP_INC := $(MINT_CROSS_TOP)/m68k-atari-mint/sys-root-old/usr/include/c++/$(LIBSTDCPP_VER)
ifneq ($(wildcard $(MINT_LIBSTDCPP_INC)/list),)
INCLUDES += -I$(MINT_LIBSTDCPP_INC) -I$(MINT_LIBSTDCPP_INC)/m68k-atari-mint
endif
endif

# m68k fastcall ABI (a2/d2 args, d0 return): must match at compile and link time.
FASTCALL ?= 1
FASTCALL_FLAGS :=
ifeq ($(FASTCALL),1)
FASTCALL_FLAGS = -mfastcall
endif

# m68k CPU target. Empty (default) = 68000/68010 code, runs on every ST/STE.
# M68K_CPU=68020-60 builds for 68020…68060 (TT, Falcon, accelerator boards).
#
# -msoft-float is not optional here: gcc's -m68020-60 implies -m68881, and this
# toolchain ships no soft-float 020 multilib, so its m68020-60 libc/crt0 demand
# a coprocessor. Without it the binary dies before main() on an FPU-less machine
# ("This program requires a 68881 or higher arithmetic coprocessor"), which looks
# like an instant drop back to the desktop. Soft-float keeps the 020 integer
# codegen (muls.l, bitfield ops) and matches the FPU-free ABI.
#
# Deliberately NOT added to LINKFLAGS: the link step must keep selecting the
# default (68000, FPU-free) multilib. That library code runs unchanged on 020+.
M68K_CPU ?=
CPU_FLAGS :=
ASM_CPU_FLAGS = -m68000
ifneq ($(strip $(M68K_CPU)),)
CPU_FLAGS = -m$(M68K_CPU) -msoft-float
# GNU as has no -m68020-60; -m68020 accepts the common 020+ instruction set.
ASM_CPU_FLAGS = -m68020
endif

LIBC_RUNTIME ?= libcmini

# Keep frame pointer chain for Hatari profiler / backtraces (-O3 omits by default).
FRAME_POINTERS ?= 0
FRAME_POINTER_FLAGS :=
ifeq ($(FRAME_POINTERS),1)
FRAME_POINTER_FLAGS = -fno-omit-frame-pointer
endif

# Common compiler flags (GCC style)
CXXFLAGS_COMMON = -Wall -Wextra -fno-exceptions
CXXFLAGS_COMMON += $(INCLUDES)
CXXFLAGS_COMMON += -Wno-unused-parameter -Wno-sign-compare -Wno-deprecated-declarations
CXXFLAGS_COMMON += -std=c++17 -fpermissive
CXXFLAGS_COMMON += $(FASTCALL_FLAGS)
CXXFLAGS_COMMON += $(CPU_FLAGS)
CXXFLAGS_COMMON += $(FRAME_POINTER_FLAGS)

# Note: Windows-specific libraries may not be available for m68k target
# You may need to provide alternatives or stub implementations
WINDOWS_LIBS = -lwinmm -lws2_32

# Platform-specific libraries
ifeq ($(PLATFORM_LIB),ATARILIB)
# Atari ST/MiNT libraries
# Standard C and math libraries are linked automatically by default
# Only add if we need specific libraries
PLATFORM_LIBS = 
else
# Windows libraries
PLATFORM_LIBS = $(WINDOWS_LIBS)
endif

ifeq ($(PLATFORM_LIB),ATARILIB)
LIBCMINI_PREFIX ?= $(HOME)/opt/cross-mint/m68k-atari-mint/sys-root/usr
LIBCMINI_BASE_DIR ?= $(LIBCMINI_PREFIX)/lib
LIBCMINI_LIB_DIR ?= $(LIBCMINI_BASE_DIR)
LIBCMINI_CRT0 ?= $(LIBCMINI_BASE_DIR)/crt0.o
# Resolve compiler runtime library dirs with the same ABI flags used at link time.
# Without FASTCALL_FLAGS here, -lgcc/-lstdc++/-lsupc++ come from the cdecl tree while
# libcmini is mfastcall, and early libsupc++ ctors call getenv() with the wrong convention.
GCC_LIB_DIR := $(dir $(shell "$(CXX)" $(FASTCALL_FLAGS) -print-file-name=libstdc++.a))
SYSROOT_LIB_DIR := $(dir $(shell "$(CXX)" $(FASTCALL_FLAGS) -print-file-name=crt0.o))

ifeq ($(FASTCALL),1)
LIBC_RUNTIME_SUBDIR = mfastcall
else
LIBC_RUNTIME_SUBDIR =
endif

ifneq ($(strip $(LIBC_RUNTIME_SUBDIR)),)
ifneq ($(wildcard $(LIBCMINI_BASE_DIR)/$(LIBC_RUNTIME_SUBDIR)/libcmini.a),)
override LIBCMINI_LIB_DIR := $(LIBCMINI_BASE_DIR)/$(LIBC_RUNTIME_SUBDIR)
endif
ifneq ($(wildcard $(SYSROOT_LIB_DIR)$(LIBC_RUNTIME_SUBDIR)/libc.a),)
SYSROOT_LIB_DIR := $(SYSROOT_LIB_DIR)$(LIBC_RUNTIME_SUBDIR)/
endif
endif

LIBCMINI_FOUND := 0
ifneq ($(wildcard $(LIBCMINI_LIB_DIR)/libcmini.a),)
ifneq ($(wildcard $(LIBCMINI_CRT0)),)
LIBCMINI_FOUND := 1
endif
endif

USE_LIBCMINI := 0
ifeq ($(LIBC_RUNTIME),libcmini)
ifeq ($(LIBCMINI_FOUND),1)
USE_LIBCMINI := 1
endif
endif
ifeq ($(LIBC_RUNTIME),auto)
ifeq ($(LIBCMINI_FOUND),1)
USE_LIBCMINI := 1
endif
endif

ifeq ($(USE_LIBCMINI),1)
DEFINES += LIBCMINI
endif
endif

# _DEBUG enables DBG_* logging (gated further by LOGGING_LEVEL). Always on;
# NDEBUG / -DDEBUG remain the Debug-vs-Release switches for asserts and extra checks.
DEFINES += _DEBUG
# LOGGING_LEVEL: optional override of common/debugstring.h default (5).
#   0=none 1=fatal 2=error 3=warn 4=info 5=debug 6=trace
#   make LOGGING_LEVEL=3   # WARN and above only
ifneq ($(LOGGING_LEVEL),)
DEFINES += LOGGING_LEVEL=$(LOGGING_LEVEL)
endif

# Debug configuration
ifeq ($(CONFIG),Debug)
# -O2 keeps libstdc++ iostream dead-strippable under --gc-sections; -O0 leaves
# references that pull MiNT-only POSIX symbols (writev, poll, …) from libstdc++.a.
CXXFLAGS = $(CXXFLAGS_COMMON) -O2 -g -DDEBUG
CXXFLAGS += -ffunction-sections -fdata-sections
LDFLAGS = -g
LIBS = $(PLATFORM_LIBS)
else
# Release configuration
DEFINES += NDEBUG
# e.g. make CXX_OPT_REL=-O3 CONFIG=release ...
CXX_OPT_REL ?= -O3
WITH_SYMBOLS ?= 1
CXXFLAGS = $(CXXFLAGS_COMMON) $(CXX_OPT_REL) -DNDEBUG
CXXFLAGS += -ffunction-sections -fdata-sections
CXXFLAGS += -fno-unwind-tables -fno-asynchronous-unwind-tables
LDFLAGS = -s
# Optional: keep debug info and do not strip (nm / addr2line / gdb on host tooling).
# Frame-bar profiler: PROFILE_FRAME_BAR defaults to 1 for ATARILIB (-DST_FRAME_BAR_PROFILE).
#   make USE_ATARILIB=1 CONFIG=release WITH_SYMBOLS=1 PROFILE_FRAME_BAR=1 OUTDIR=...
# (Pass PROFILE_FRAME_BAR=1 if your environment exported PROFILE_FRAME_BAR=0.)
ifeq ($(WITH_SYMBOLS),1)
CXXFLAGS += -g
override LDFLAGS :=
endif
# Note: --gc-sections may not be supported for m68k
# LDFLAGS += -Wl,--gc-sections -Wl,--strip-all
LIBS = $(PLATFORM_LIBS)
endif

# Convert defines to compiler flags
DEFINE_FLAGS = $(foreach def,$(DEFINES),-D$(def))

# Assembly flags (GAS - GNU Assembler for m68k)
# Note: Assembly files may be x86-specific and may need conversion
ASMFLAGS = $(ASM_CPU_FLAGS)
ifeq ($(CONFIG),Debug)
ASMFLAGS += -g
endif
ifeq ($(WITH_SYMBOLS),1)
ASMFLAGS += -g
endif

# Resource compiler flags (windres style)
RCFLAGS = -O coff -D_DEBUG=1
ifneq ($(CONFIG),Debug)
RCFLAGS += -DNDEBUG=1
endif

# Linker flags (GCC style)
# Platform-specific linking
ifeq ($(PLATFORM_LIB),ATARILIB)
# Atari ST: Build as TOS executable, not DLL
# Note: -nostdlib removed - we need standard library for TOS executables
# Entry point will be main() from startup.cpp
# No special entry point needed - standard C++ main() is fine
ifeq ($(USE_LIBCMINI),1)
RUNTIME_LINKFLAGS += -nostdlib
RUNTIME_LINKFLAGS += -L$(LIBCMINI_LIB_DIR)
RUNTIME_LINKFLAGS += -L$(SYSROOT_LIB_DIR)
RUNTIME_LINKFLAGS += -L$(GCC_LIB_DIR)
LIBS += -Wl,--start-group -lgcc -lsupc++ -lstdc++ -lcmini -lm -lgcc -Wl,--end-group
LINK_PRE_OBJS += $(LIBCMINI_CRT0)
else
ifeq ($(filter help clean cleanall remix-web remix-web-itch,$(MAKECMDGOALS)),)
ifeq ($(LIBC_RUNTIME),libcmini)
$(error LIBC_RUNTIME=libcmini requested, but libcmini was not found. Expected $(LIBCMINI_CRT0) and $(LIBCMINI_LIB_DIR)/libcmini.a)
endif
endif
endif
RUNTIME_LINKFLAGS += -Wl,--gc-sections
else
# Windows: Build as DLL
LDFLAGS += -shared
LDFLAGS += -Wl,-soname,$(TARGET_NAME).dll
endif

# Link driver flags: LDFLAGS plus ABI/runtime flags. Runtime flags are kept separate because
# Release WITH_SYMBOLS uses "override LDFLAGS :=" which intentionally clears strip flags.
LINKFLAGS = $(LDFLAGS) $(RUNTIME_LINKFLAGS) $(FASTCALL_FLAGS)

# Generate import library (may not be supported on all targets)
# Will be handled separately if needed

#=============================================================================
# SOURCE FILES
#=============================================================================

# Common library C++ sources (platform-agnostic)
COMMONLIB_CPP_SOURCES = \
	COMMONLIB/coorda.cpp

# Vanilla Conquer common/ sources (replacing removed tiberiandawn/*.cpp on ATARI_ST)
ifeq ($(PLATFORM_LIB),ATARILIB)
ifeq ($(USE_LIBCMINI),1)
PATHS_IMPL_CPP = paths_tos.cpp
PATHS_IMPL_OBJ = $(INTDIR)/paths_tos.o
else
PATHS_IMPL_CPP = paths_posix.cpp
PATHS_IMPL_OBJ = $(INTDIR)/paths_posix.o
endif

VC_COMMON_CPP_SOURCES = \
	../common/b64pipe.cpp ../common/b64straw.cpp ../common/base64.cpp \
	../common/bfiofile.cpp ../common/blowfish.cpp ../common/blowpipe.cpp \
	../common/blwstraw.cpp ../common/buff.cpp ../common/ccfile.cpp \
	../common/cdfile.cpp ../common/combuf.cpp ../common/control.cpp \
	../common/crc.cpp 	../common/crew.cpp ../common/debugstring.cpp \
	../common/fading.cpp ../common/field.cpp ../common/file.cpp \
	../common/file_posix.cpp ../common/framelimit.cpp ../common/fixed.cpp \
	../common/hsv.cpp ../common/ini.cpp ../common/int.cpp \
	../common/lcw.cpp ../common/lcwpipe.cpp ../common/lcwstraw.cpp \
	../common/link.cpp ../common/load.cpp ../common/misc.cpp \
	../common/dipthong.cpp ../common/_diptabl.cpp ../common/face.cpp ../common/irandom.cpp ../common/newdel.cpp \
	../common/mixfile.cpp ../common/mp.cpp ../common/palettec.cpp ../common/paths.cpp \
	../common/$(PATHS_IMPL_CPP) ../common/pipe.cpp ../common/pk.cpp \
	../common/pkpipe.cpp ../common/pkstraw.cpp ../common/random.cpp \
	../common/rawfile.cpp ../common/readline.cpp ../common/rgb.cpp \
	../common/rndstraw.cpp ../common/settings.cpp ../common/sha.cpp \
	../common/shapipe.cpp ../common/shastraw.cpp ../common/straw.cpp \
	../common/toggle.cpp ../common/timer.cpp ../common/timer_st_vbl.cpp ../common/timerdwn.cpp ../common/vector.cpp \
	../common/wwkeyboard.cpp \
	../common/xpipe.cpp ../common/xstraw.cpp ../common/gadget.cpp
else
VC_COMMON_CPP_SOURCES =
endif

# IPX sources - ipx95.cpp is used for all modern builds
# ipx.cpp was removed as it only contained DOS/DPMI-specific code
ifeq ($(PLATFORM_LIB),ATARILIB)
IPX_SOURCES =
else
IPX_SOURCES = IPX95.CPP
endif

# Root directory C++ sources
# Note: dde.cpp and ccdde.cpp are Windows-only (DDE is Windows-specific)
# All files use uppercase .CPP extension (case-insensitive filesystem handles this)
ROOT_CPP_SOURCES = \
	AADATA.CPP ABSTRACT.CPP ADATA.CPP AIRCRAFT.CPP ALLOC.CPP ANIM.CPP \
	AUDIO.CPP BASE.CPP BBDATA.CPP BDATA.CPP BUILDING.CPP BULLET.CPP \
	CARGO.CPP CCINI.CPP $(if $(filter WIN32LIB,$(PLATFORM_LIB)),CCDDE.CPP,) CCFILE.CPP CDATA.CPP CDFILE.CPP CELL.CPP \
	CHECKBOX.CPP CHEKLIST.CPP COLRLIST.CPP COMBAT.CPP COMBUF.CPP \
	COMQUEUE.CPP CONFDLG.CPP CONNECT.CPP CONQUER.CPP CONST.CPP \
	CONTROL.CPP COORD.CPP CREDITS.CPP CREW.CPP $(if $(filter WIN32LIB,$(PLATFORM_LIB)),DDE.CPP,) DEBUG.CPP \
	DESCDLG.CPP DIAL8.CPP DIALOG.CPP DISPLAY.CPP DOOR.CPP DPMI.CPP DRIVE.CPP DROP.CPP EDIT.CPP \
	ENDING.CPP EVENT.CPP EXPAND.CPP FACING.CPP FACTORY.CPP FIELD.CPP \
	FINDPATH.CPP FLASHER.CPP FLY.CPP FOOT.CPP FUSE.CPP GADGET.CPP \
	GAMEDLG.CPP GAUGE.CPP GLOBALS.CPP GOPTIONS.CPP GSCREEN.CPP \
	HDATA.CPP HEAP.CPP HELP.CPP HOUSE.CPP IDATA.CPP INFANTRY.CPP \
	INI.CPP INIT.CPP INTERNET.CPP INTERPAL.CPP INTRO.CPP IOMAP.CPP \
	IOOBJ.CPP $(IPX_SOURCES) IPXADDR.CPP IPXCONN.CPP IPXGCONN.CPP \
	IPXMGR.CPP JSHELL.CPP KEYFRAME.CPP LAYER.CPP LIB.CPP LINK.CPP LIST.CPP \
	LOADDLG.CPP LOGIC.CPP MAP.CPP MAPEDDLG.CPP MAPEDIT.CPP MAPEDPLC.CPP \
	MAPEDSEL.CPP MAPEDTM.CPP MAPSEL.CPP MENUS.CPP \
	MISSION.CPP MIXFILE.CPP MONOC.CPP MOUSE.CPP MPLAYER.CPP \
	MSGBOX.CPP MSGLIST.CPP NETDLG.CPP NOSEQCON.CPP NULLCONN.CPP \
	NULLDLG.CPP NULLMGR.CPP OBJECT.CPP ODATA.CPP OPTIONS.CPP \
	OVERLAY.CPP PACKET.CPP POWER.CPP PROFILE.CPP QUEUE.CPP RADAR.CPP \
	RADIO.CPP REINF.CPP RULES.CPP SAVELOAD.CPP \
	SCENARIO.CPP SCENARIOINI.CPP SCORE.CPP SCROLL.CPP SDATA.CPP SEQCONN.CPP Shape.cpp \
	SHAPEBTN.CPP SIDEBAR.CPP SIDEBARGlyphx.CPP SLIDER.CPP SMUDGE.CPP \
	SOUNDDLG.CPP SPECIAL.CPP STATS.CPP SUPER.CPP TAB.CPP \
	TARCOM.CPP TARGET.CPP TDATA.CPP TEAM.CPP TEAMTYPE.CPP \
	TECHNO.CPP TEMPLATE.CPP TERRAIN.CPP TEXTBTN.CPP THEME.CPP \
	TOGGLE.CPP TRIGGER.CPP TURRET.CPP TXTLABEL.CPP UDATA.CPP UNIT.CPP \
	UTRACKER.CPP VECTOR.CPP 	VISUDLG.CPP WINSTUB.CPP wsa.cpp

# Root sources removed during VC modernization (now in ../common/ or dropped on ATARI_ST)
ifeq ($(PLATFORM_LIB),ATARILIB)
ATARI_ST_ROOT_EXCLUDES = \
	ALLOC.CPP CCFILE.CPP CDFILE.CPP COMBUF.CPP COMQUEUE.CPP CONNECT.CPP \
	CONTROL.CPP CREW.CPP DESCDLG.CPP DPMI.CPP FIELD.CPP GADGET.CPP INI.CPP \
	INTERPAL.CPP INTERNET.CPP IPX95.CPP IPXCONN.CPP IPXGCONN.CPP IPXMGR.CPP \
	IPXADDR.CPP LINK.CPP MIXFILE.CPP MPLAYER.CPP NETDLG.CPP NOSEQCON.CPP \
	NULLCONN.CPP NULLMGR.CPP PACKET.CPP RAND.CPP RAWFILE.CPP \
	SEQCONN.CPP TOGGLE.CPP UTRACKER.CPP VECTOR.CPP
ROOT_CPP_SOURCES := $(filter-out $(ATARI_ST_ROOT_EXCLUDES),$(ROOT_CPP_SOURCES))
endif

# Platform library C++ sources (WIN32LIB or ATARILIB)
# ATARILIB files use lowercase .cpp (as tracked in git), WIN32LIB files use uppercase .CPP
PLATFORM_LIB_CPP_SOURCES = \
	$(PLATFORM_LIB)/alloc.cpp \
	$(PLATFORM_LIB)/drawbuff.cpp $(PLATFORM_LIB)/st16_iconset.cpp $(PLATFORM_LIB)/st16_draw.cpp $(PLATFORM_LIB)/st16_convert.cpp $(PLATFORM_LIB)/st16_preshift.cpp $(PLATFORM_LIB)/st_planar_draw.cpp $(PLATFORM_LIB)/st_blit.cpp $(PLATFORM_LIB)/st_blitter_hw.cpp $(PLATFORM_LIB)/st_soft_blit.cpp $(PLATFORM_LIB)/st_cache.cpp $(PLATFORM_LIB)/st_cache_ops.cpp $(PLATFORM_LIB)/st_sprite_cache.cpp $(PLATFORM_LIB)/shpx.cpp $(PLATFORM_LIB)/font.cpp $(PLATFORM_LIB)/gbuffer.cpp \
	$(PLATFORM_LIB)/getshape.cpp $(PLATFORM_LIB)/iconset.cpp $(PLATFORM_LIB)/iff.cpp $(PLATFORM_LIB)/ikbd.cpp \
	$(PLATFORM_LIB)/keyboard.cpp $(PLATFORM_LIB)/wwkeyboard_st.cpp \
	$(PLATFORM_LIB)/mouseww.cpp \
	$(PLATFORM_LIB)/palette.cpp $(if $(filter ATARILIB,$(PLATFORM_LIB)),$(PLATFORM_LIB)/c2p.cpp $(PLATFORM_LIB)/st_temperat_palette_data.cpp,) \
	$(PLATFORM_LIB)/timerini.cpp \
	$(if $(filter WIN32LIB,$(PLATFORM_LIB)),$(PLATFORM_LIB)/WINDOWS.CPP,) $(PLATFORM_LIB)/writepcx.cpp \
	$(PLATFORM_LIB)/startup.cpp \
	$(if $(filter WIN32LIB,$(PLATFORM_LIB)),$(PLATFORM_LIB)/MiscAsm.cpp,) \
	$(if $(filter WIN32LIB,$(PLATFORM_LIB)),$(PLATFORM_LIB)/tcpip.cpp,) \
	$(if $(filter WIN32LIB,$(PLATFORM_LIB)),$(PLATFORM_LIB)/DLLInterface.cpp,) \
	$(if $(filter ATARILIB,$(PLATFORM_LIB)),$(PLATFORM_LIB)/direct.cpp $(PLATFORM_LIB)/misc.cpp $(PLATFORM_LIB)/st_gitinfo.cpp $(PLATFORM_LIB)/st_link_stubs.cpp $(PLATFORM_LIB)/st_hw_probe.cpp $(PLATFORM_LIB)/windows_time.cpp $(PLATFORM_LIB)/title_pcx_load.cpp $(PLATFORM_LIB)/audio_ste.cpp $(PLATFORM_LIB)/ste_stream_pcm.cpp $(PLATFORM_LIB)/ste_stream_ima99.cpp $(PLATFORM_LIB)/ws_adpcm_68k.cpp $(PLATFORM_LIB)/st_frame_meter.cpp $(PLATFORM_LIB)/st_playback_timing.cpp $(PLATFORM_LIB)/st_screen.cpp $(PLATFORM_LIB)/libcmini_posix_shim.cpp $(PLATFORM_LIB)/audx/audx_pool_file.cpp $(PLATFORM_LIB)/audx/audx_page_cache.cpp $(PLATFORM_LIB)/audx/ste_stream_memory_source.cpp $(PLATFORM_LIB)/audx/ste_stream_cache_source.cpp $(PLATFORM_LIB)/audx/ste_stream_file_source.cpp,)

# Shared STVQ runtime (C) — game + tools/stvqview
ifeq ($(PLATFORM_LIB),ATARILIB)
STVQ_C_SOURCES = \
	$(PLATFORM_LIB)/stvq/stvq_format.c \
	$(PLATFORM_LIB)/stvq/stvq_hw.c \
	$(PLATFORM_LIB)/stvq/stvq_player.c \
	$(PLATFORM_LIB)/stvq/stvq_prof.c
STVQ_C_OBJS = $(addprefix $(INTDIR)/,$(notdir $(STVQ_C_SOURCES:.c=.o)))
else
STVQ_C_SOURCES =
STVQ_C_OBJS =
endif

# Assembly sources (platform-specific)
# Root directory files use uppercase .ASM, ATARILIB files use lowercase .asm (as tracked in git)
ASM_SOURCES = \
	IPXPROT.ASM IPXREAL.ASM KEYFBUFF.ASM MMX.ASM PAGFAULT.ASM \
	SUPPORT.ASM TXTPRNT.ASM WINASM.ASM
ifeq ($(PLATFORM_LIB),ATARILIB)
ASM_SOURCES += $(PLATFORM_LIB)/linea_shim.s
endif

# Resource files
RC_SOURCES = \
	RESOURCE/TiberianDawn.rc

# All C++ sources
ALL_CPP_SOURCES = $(COMMONLIB_CPP_SOURCES) $(VC_COMMON_CPP_SOURCES) $(ROOT_CPP_SOURCES) $(PLATFORM_LIB_CPP_SOURCES)

# Generate object file lists (GCC uses .o extension)
# All object files go to $(INTDIR) with just the basename
# NOTE: alloc.cpp exists in both root and platform lib. The project uses both.
# We'll use unique names: alloc.o for root, alloc_platformlib.o for platform lib
# All source files now use uppercase .CPP extension (case-insensitive filesystem handles this)
# Handle both .cpp and .CPP extensions in object file generation
COMMONLIB_CPP_OBJS = $(addprefix $(INTDIR)/,$(notdir $(COMMONLIB_CPP_SOURCES:.cpp=.o)))
VC_COMMON_CPP_OBJS = $(addprefix $(INTDIR)/,$(notdir $(VC_COMMON_CPP_SOURCES:.cpp=.o)))
ROOT_CPP_OBJS = $(addprefix $(INTDIR)/,$(notdir $(patsubst %.CPP,%.o,$(patsubst %.cpp,%.o,$(ROOT_CPP_SOURCES)))))
# Git tracks root sources as lowercase .cpp; object basenames must match for GNU make.
ROOT_CPP_OBJS := $(foreach obj,$(ROOT_CPP_OBJS),$(INTDIR)/$(shell echo $(notdir $(obj)) | tr '[:upper:]' '[:lower:]'))
PLATFORM_LIB_CPP_OBJS = $(addprefix $(INTDIR)/,$(notdir $(patsubst %.CPP,%.o,$(patsubst %.cpp,%.o,$(PLATFORM_LIB_CPP_SOURCES)))))
# Handle alloc.cpp conflict: root version becomes alloc.o, platform lib version becomes alloc_platformlib.o
PLATFORM_LIB_CPP_OBJS := $(subst $(INTDIR)/alloc.o,$(INTDIR)/alloc_$(PLATFORM_LIB).o,$(PLATFORM_LIB_CPP_OBJS))
# iconset.cpp exists in root (PC le32toh) and ATARILIB/ (ST16 native map) — keep platform object
PLATFORM_LIB_CPP_OBJS := $(subst $(INTDIR)/iconset.o,$(INTDIR)/iconset_$(PLATFORM_LIB).o,$(PLATFORM_LIB_CPP_OBJS))
# misc.cpp exists in ../common/ (VC_COMMON) and ATARILIB/ — keep both objects
PLATFORM_LIB_CPP_OBJS := $(subst $(INTDIR)/misc.o,$(INTDIR)/misc_$(PLATFORM_LIB).o,$(PLATFORM_LIB_CPP_OBJS))
# gadget.cpp exists in ../common/ (VC_COMMON) and root GADGET.CPP — root excluded on ST
CPP_OBJECTS = $(COMMONLIB_CPP_OBJS) $(VC_COMMON_CPP_OBJS) $(ROOT_CPP_OBJS) $(PLATFORM_LIB_CPP_OBJS)
ASM_OBJECTS = $(addprefix $(INTDIR)/,$(notdir $(patsubst %.ASM,%.o,$(patsubst %.asm,%.o,$(patsubst %.s,%.o,$(ASM_SOURCES))))))
# Filter out truncated x86 assembly object files for ATARILIB (they can't be assembled for m68k)
ifeq ($(PLATFORM_LIB),ATARILIB)
ASM_OBJECTS := $(filter-out $(INTDIR)/IPXPROT.o $(INTDIR)/IPXREAL.o $(INTDIR)/KEYFBUFF.o $(INTDIR)/MMX.o $(INTDIR)/PAGFAULT.o $(INTDIR)/SUPPORT.o $(INTDIR)/TXTPRNT.o $(INTDIR)/WINASM.o,$(ASM_OBJECTS))
endif
RC_OBJECTS = $(addprefix $(INTDIR)/,$(notdir $(RC_SOURCES:.rc=.o)))
# Filter out resource object files for ATARILIB (windres not available/needed for m68k)
ifeq ($(PLATFORM_LIB),ATARILIB)
RC_OBJECTS :=
endif

ALL_OBJECTS = $(CPP_OBJECTS) $(ASM_OBJECTS) $(RC_OBJECTS) $(STVQ_C_OBJS)

# Output files - platform-specific
ifeq ($(PLATFORM_LIB),ATARILIB)
# Atari ST: TOS executable (lowercase .tos extension)
TARGET_EXE = $(OUTDIR)/$(TARGET_NAME).tos
TARGET_DLL = $(TARGET_EXE)  # Alias for compatibility
TARGET_LIB = $(OUTDIR)/$(TARGET_NAME).lib
TARGET_EXP = $(OUTDIR)/$(TARGET_NAME).exp
else
# Windows: DLL
TARGET_DLL = $(OUTDIR)/$(TARGET_NAME).dll
TARGET_LIB = $(OUTDIR)/$(TARGET_NAME).lib
TARGET_EXP = $(OUTDIR)/$(TARGET_NAME).exp
TARGET_EXE = $(TARGET_DLL)  # Alias for compatibility
endif

#=============================================================================
# BUILD RULES
#=============================================================================

.PHONY: all clean cleanall help st-tests itch-release

.DEFAULT_GOAL := all

# Default target
all: $(TARGET_EXE)
	@echo "Build complete: $(TARGET_EXE)"

# Main executable/DLL target
ifeq ($(PLATFORM_LIB),ATARILIB)
# Atari ST: Build TOS executable
$(TARGET_EXE): $(ALL_OBJECTS) | $(OUTDIR) $(INTDIR)
	@echo "Linking $(TARGET_EXE)..."
	$(CXX) $(LINKFLAGS) -o $(TARGET_EXE) $(LINK_PRE_OBJS) $(ALL_OBJECTS) $(LIBS)
	@echo "TOS executable created: $(TARGET_EXE)"
else
# Windows: Build DLL
$(TARGET_DLL): $(ALL_OBJECTS) | $(OUTDIR)
	@echo "Linking $(TARGET_DLL)..."
	$(CXX) $(LINKFLAGS) -o $(TARGET_DLL) $(ALL_OBJECTS) $(LIBS)
	@echo "Creating import library $(TARGET_LIB)..."
	@$(AR) rcs $(TARGET_LIB) $(ALL_OBJECTS) 2>/dev/null || echo "Note: Import library creation skipped"
endif

# Create output directory
$(OUTDIR):
	@mkdir -p $(OUTDIR)

# Create intermediate directory
$(INTDIR):
	@mkdir -p $(INTDIR)

# C++ compilation rules for common library files
# Handle both .cpp and .CPP extensions (case-insensitive filesystem)
$(INTDIR)/%.o: COMMONLIB/%.cpp | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<
$(INTDIR)/%.o: COMMONLIB/%.CPP | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

# Vanilla Conquer common/ sources (explicit list only — do not steal ATARILIB basenames)
ifeq ($(PLATFORM_LIB),ATARILIB)
$(VC_COMMON_CPP_OBJS): $(INTDIR)/%.o : ../common/%.cpp | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<
endif

# C++ compilation rules for root directory files
# Handle both .cpp and .CPP extensions (case-insensitive filesystem)
$(INTDIR)/%.o: %.cpp | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<
$(INTDIR)/%.o: %.CPP | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

# C++ compilation rules for platform library files
# Handle both .cpp and .CPP extensions (case-insensitive filesystem)
$(INTDIR)/%.o: $(PLATFORM_LIB)/%.cpp | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<
$(INTDIR)/%.o: $(PLATFORM_LIB)/%.CPP | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

# STVQ shared C runtime (atarilib/stvq/)
ifeq ($(PLATFORM_LIB),ATARILIB)
CFLAGS_STVQ = -Wall -Wextra -std=c99 $(INCLUDES) $(FASTCALL_FLAGS) $(CPU_FLAGS) $(FRAME_POINTER_FLAGS)
ifeq ($(CONFIG),Debug)
CFLAGS_STVQ += -O2 -g -DDEBUG
else
CFLAGS_STVQ += $(CXX_OPT_REL) -DNDEBUG
ifeq ($(WITH_SYMBOLS),1)
CFLAGS_STVQ += -g
endif
endif
$(INTDIR)/stvq_format.o: $(PLATFORM_LIB)/stvq/stvq_format.c | $(INTDIR)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS_STVQ) $(DEFINE_FLAGS) -o $@ -c $<
$(INTDIR)/stvq_hw.o: $(PLATFORM_LIB)/stvq/stvq_hw.c | $(INTDIR)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS_STVQ) $(DEFINE_FLAGS) -o $@ -c $<
$(INTDIR)/stvq_player.o: $(PLATFORM_LIB)/stvq/stvq_player.c | $(INTDIR)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS_STVQ) $(DEFINE_FLAGS) -o $@ -c $<
$(INTDIR)/stvq_prof.o: $(PLATFORM_LIB)/stvq/stvq_prof.c | $(INTDIR)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS_STVQ) $(DEFINE_FLAGS) -o $@ -c $<

# AUDX runtime (atarilib/audx/)
$(INTDIR)/audx_pool_file.o: $(PLATFORM_LIB)/audx/audx_pool_file.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<
$(INTDIR)/audx_page_cache.o: $(PLATFORM_LIB)/audx/audx_page_cache.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<
$(INTDIR)/ste_stream_memory_source.o: $(PLATFORM_LIB)/audx/ste_stream_memory_source.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<
$(INTDIR)/ste_stream_cache_source.o: $(PLATFORM_LIB)/audx/ste_stream_cache_source.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<
$(INTDIR)/ste_stream_file_source.o: $(PLATFORM_LIB)/audx/ste_stream_file_source.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<
endif

# Special rule for platform lib alloc.cpp to avoid conflict with root alloc.cpp
$(INTDIR)/alloc_$(PLATFORM_LIB).o: $(PLATFORM_LIB)/ALLOC.CPP | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

# ATARILIB misc.cpp (AllSurfaces, Wait_Vert_Blank, Calculate_CRC, …) vs common/misc.cpp
$(INTDIR)/misc_ATARILIB.o: ATARILIB/misc.cpp | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

# ATARILIB iconset.cpp (ST16 Get_Icon_Set_Map) vs root iconset.cpp (PC le32toh)
$(INTDIR)/iconset_ATARILIB.o: ATARILIB/iconset.cpp | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

# cpushl/cinvl live in st_cache_ops.cpp; st_cache.cpp probes _CPU cookie at runtime.
$(INTDIR)/st_cache_ops.o: ATARILIB/st_cache_ops.cpp | $(INTDIR)
	@echo "Compiling $< (68040 cache line ops)..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -mcpu=68040 -o $@ -c $<

# Explicit rules for files to work around pattern matching issue with path separators in INTDIR
ifneq ($(PLATFORM_LIB),ATARILIB)
$(INTDIR)/mixfile.o: MIXFILE.CPP | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<
endif

$(INTDIR)/init.o: INIT.CPP | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/jshell.o: JSHELL.CPP | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

# Assembly compilation rules for root directory files
# WARNING: These assembly files are x86-specific and will NOT work with m68k assembler
# You will need to either:
# 1. Convert them to m68k assembly, or
# 2. Rewrite the functions in C/C++, or
# 3. Skip them if not critical
# Handle both .asm and .ASM extensions (case-insensitive filesystem)
$(INTDIR)/%.o: %.asm | $(INTDIR)
	@echo "Assembling $< (WARNING: x86 assembly on m68k target)..."
	@mkdir -p $(INTDIR)
	@echo "Error: x86 assembly files cannot be assembled for m68k target"
	@echo "Skipping $< - you may need to convert or rewrite in C/C++"
	@touch $@
$(INTDIR)/%.o: %.ASM | $(INTDIR)
	@echo "Assembling $< (WARNING: x86 assembly on m68k target)..."
	@mkdir -p $(INTDIR)
	@echo "Error: x86 assembly files cannot be assembled for m68k target"
	@echo "Skipping $< - you may need to convert or rewrite in C/C++"
	@touch $@

# Assembly compilation rules for platform library files
# Handle both .asm and .ASM extensions (case-insensitive filesystem)
# ATARILIB uses lowercase .asm, WIN32LIB uses uppercase .ASM
$(INTDIR)/%.o: $(PLATFORM_LIB)/%.s | $(INTDIR)
	@echo "Assembling $<..."
	@mkdir -p $(INTDIR)
	@if [ "$(PLATFORM_LIB)" = "ATARILIB" ]; then \
		$(ASM) $(ASMFLAGS) -o $@ $<; \
	else \
		echo "WARNING: x86 assembly on m68k target"; \
		echo "Error: x86 assembly files cannot be assembled for m68k target"; \
		echo "Skipping $< - you may need to convert or rewrite in C/C++"; \
		touch $@; \
	fi
$(INTDIR)/%.o: $(PLATFORM_LIB)/%.asm | $(INTDIR)
	@echo "Assembling $<..."
	@mkdir -p $(INTDIR)
	@if [ "$(PLATFORM_LIB)" = "ATARILIB" ]; then \
		$(ASM) $(ASMFLAGS) -o $@ $<; \
	else \
		echo "WARNING: x86 assembly on m68k target"; \
		echo "Error: x86 assembly files cannot be assembled for m68k target"; \
		echo "Skipping $< - you may need to convert or rewrite in C/C++"; \
		touch $@; \
	fi
$(INTDIR)/%.o: $(PLATFORM_LIB)/%.ASM | $(INTDIR)
	@echo "Assembling $<..."
	@mkdir -p $(INTDIR)
	@if [ "$(PLATFORM_LIB)" = "ATARILIB" ]; then \
		$(ASM) $(ASMFLAGS) -o $@ $<; \
	else \
		echo "WARNING: x86 assembly on m68k target"; \
		echo "Error: x86 assembly files cannot be assembled for m68k target"; \
		echo "Skipping $< - you may need to convert or rewrite in C/C++"; \
		touch $@; \
	fi

# Resource compilation rule
# Skip if windres is not available (RC will be ":")
$(INTDIR)/%.o: RESOURCE/%.rc | $(INTDIR)
	@echo "Compiling resources $<..."
	@mkdir -p $(INTDIR)
	@if [ -f $(CROSS_PREFIX)/$(CROSS_TARGET)-windres ]; then \
		$(CROSS_PREFIX)/$(CROSS_TARGET)-windres $(RCFLAGS) -i $< -o $@; \
	else \
		echo "Warning: windres not found, skipping $<"; \
		touch $@; \
	fi

# Clean build artifacts for current configuration
clean:
	@echo "Cleaning $(CONFIG) build..."
	@rm -rf $(INTDIR)
	@rm -f $(OUTDIR)/$(TARGET_NAME).dll
	@rm -f $(OUTDIR)/$(TARGET_NAME).tos
	@rm -f $(OUTDIR)/$(TARGET_NAME).lib
	@rm -f $(OUTDIR)/$(TARGET_NAME).exp
	# Clean both release and debug builds (using d suffix for debug)
	@rm -f $(OUTDIR)/$(PROJECT_NAME).dll
	@rm -f $(OUTDIR)/$(PROJECT_NAME)d.dll
	@rm -f $(OUTDIR)/$(PROJECT_NAME).lib
	@rm -f $(OUTDIR)/$(PROJECT_NAME)d.lib
	@rm -f $(OUTDIR)/$(PROJECT_NAME).exp
	@rm -f $(OUTDIR)/$(PROJECT_NAME)d.exp
	@rm -f $(OUTDIR)/cnc.tos
	@rm -f $(OUTDIR)/cncd.tos
	@rm -f $(OUTDIR)/cnc_st_tests.tos
	@rm -f $(OUTDIR)/tstcnc.tos
	@rm -f $(OUTDIR)/tstcncd.tos
	@rm -f $(INTDIR)/st_tests_main.o $(INTDIR)/st_autotests.o $(INTDIR)/st_c2p_autotests.o $(INTDIR)/st_terrain_tile_left_clip_autotest.o $(INTDIR)/st16_convert_autotest.o $(INTDIR)/st_iconset_grid_interactive.o $(INTDIR)/st_interactive.o $(INTDIR)/st_blitter_rect_interactive.o $(INTDIR)/st_pcx_minimal.o $(INTDIR)/st_mix_minimal.o $(INTDIR)/st_text.o $(INTDIR)/st_title_production.o $(INTDIR)/st_build_frame_assets.o $(INTDIR)/st_font_browser.o $(INTDIR)/st_game_link_stubs.o $(INTDIR)/st_audio_asset_autotest.o $(INTDIR)/st_cps_browser.o $(INTDIR)/title_pcx_load_st_tests.o $(INTDIR)/st_temperat_palette_data.o $(INTDIR)/st_blit.o $(INTDIR)/st_blitter_hw.o $(INTDIR)/st_soft_blit.o

# Clean all configurations
cleanall:
	@echo "Cleaning all builds..."
	@rm -rf Win32 x64
	@rm -rf $(OUTDIR)

# Help target
help:
	@echo "Tiberian Dawn Makefile (GNU Cross-Compiler)"
	@echo ""
	@echo "Usage:"
	@echo "  make [CONFIG=Debug|Release] [target]"
	@echo ""
	@echo "Targets:"
ifeq ($(PLATFORM_LIB),ATARILIB)
	@echo "  all          - Build the TOS executable (default)"
	@echo "  st-tests     - Build tstcnc(.d).tos (on-machine / interactive suite)"
	@echo "  itch-release - Tagged clean-tree release zips for itch.io (see below)"
else
	@echo "  all      - Build the DLL (default)"
endif
	@echo "  clean    - Remove build artifacts for current configuration"
	@echo "  cleanall - Remove all build artifacts"
	@echo "  help     - Show this help message"
	@echo ""
	@echo "Configuration Variables:"
	@echo "  CONFIG=Debug   - Build debug version (default: Release)"
	@echo "  CONFIG=Release - Build release version (defaults: CXX_OPT_REL=-O3, WITH_SYMBOLS=1)"
	@echo "  PROFILE_FRAME_BAR=0 - Atari-only: disable Hz200 profiler (default: 1, -DST_FRAME_BAR_PROFILE)"
	@echo "  PROFILE_BORDER=0    - Atari-only: disable audio pen-0 border timing (default: 0)"
	@echo "  WITH_SYMBOLS=1   - Release only: add -g, omit strip (-s); default: 1"
	@echo "  FRAME_POINTERS=1 - Keep A6 frame chain (-fno-omit-frame-pointer); default: 0"
	@echo "  CXX_OPT_REL=-O3  - Release only: optimization flag (default: -O3)"
	@echo "  FASTCALL=0       - Disable -mfastcall on compile and link (default: 1)"
	@echo "  M68K_CPU=68020-60 - Atari-only: build for 68020..68060 (default: empty = 68000)"
	@echo "  LIBC_RUNTIME=libcmini|mintlib|auto - Atari-only C runtime (default: libcmini)"
	@echo "    mintlib: paths_posix.cpp; libcmini: paths_tos.cpp (-DLIBCMINI)"
	@echo "  LIBCMINI_PREFIX  - Atari-only libcmini install prefix ending in /usr"
	@echo "  PLATFORM=Win32 - Target platform (default: Win32)"
	@echo "  CROSS_PREFIX   - Cross-compiler path (default: \$$HOME/opt/cross-mint/bin)"
	@echo "  USE_ATARILIB   - Force use of ATARILIB (default: auto-detect)"
	@echo "  USE_WIN32LIB   - Force use of WIN32LIB (default: auto-detect)"
	@echo ""
	@echo "Platform Library:"
	@echo "  Using: $(PLATFORM_LIB)"
	@echo ""
	@echo "Examples:"
	@echo "  make                    - Build release version"
	@echo "  make CONFIG=Debug        - Build debug version"
	@echo "  make USE_ATARILIB=1      - Force use of ATARILIB"
	@echo "  make USE_WIN32LIB=1      - Force use of WIN32LIB"
	@echo "  make clean               - Clean current configuration"
	@echo "  make cleanall            - Clean everything"
ifeq ($(PLATFORM_LIB),ATARILIB)
	@echo "  make st-tests            - Build tstcnc(.d).tos (ST on-machine suite)"
	@echo "  make USE_ATARILIB=1 CONFIG=release CXX_OPT_REL=-O3 WITH_SYMBOLS=1 FRAME_POINTERS=1 PROFILE_FRAME_BAR=1 OUTDIR=./bin/AtariST/ -j4"
	@echo "  make LIBC_RUNTIME=mintlib -j4"
	@echo "  make LIBC_RUNTIME=libcmini LIBCMINI_PREFIX=/path/to/libcmini/usr -j4"
	@echo "  make PROFILE_FRAME_BAR=0 - Atari build without tab-strip profiler (on by default)"
	@echo "  make PROFILE_BORDER=0    - Audio VBL/IMA decode border timing (on by default)"
	@echo "  make itch-release        - dist/itch/cncst-x.y.z.zip + cncst-sym-x.y.z.zip"
	@echo "                             (requires tag release/atarist/vx.y.z on HEAD; clean repo unless ITCH_ALLOW_DIRTY=1)"
endif
	@echo ""
	@echo "Cross-Compiler:"
	@echo "  Using: $(CROSS_PREFIX)/$(CROSS_TARGET)-*"
	@echo "  CXX:   $(CXX)"
	@echo "  CC:    $(CC)"
	@echo "  ASM:   $(ASM)"
	@echo "  RC:    $(RC)"
	@echo ""
	@echo "Important Notes:"
	@if [ "$(PLATFORM_LIB)" = "ATARILIB" ]; then \
		echo "  - Using ATARILIB for Atari ST platform"; \
		echo "  - Building TOS executable (.tos file)"; \
		echo "  - Assembly files (.asm) should be m68k-compatible"; \
		if [ "$(USE_LIBCMINI)" = "1" ]; then \
			echo "  - Linking against libcmini from $(LIBCMINI_LIB_DIR)"; \
		else \
			echo "  - Linking with default MiNT runtime"; \
		fi; \
	else \
		echo "  - Using WIN32LIB for Windows platform"; \
		echo "  - Building Windows DLL (.dll file)"; \
		echo "  - Assembly files (.asm) are x86-specific"; \
		echo "  - Windows libraries (winmm, ws2_32) required"; \
	fi
	@echo "  - Resource files require windres (may not be available)"

#=============================================================================
# Atari ST on-machine test harness (tstcnc(.d).tos — own main(), not the game)
#=============================================================================
ifeq ($(PLATFORM_LIB),ATARILIB)
ST_SUITE_DIR = tests/st_suite
ifeq ($(CONFIG),Debug)
ST_TEST_EXE = $(OUTDIR)/tstcncd.tos
else
ST_TEST_EXE = $(OUTDIR)/tstcnc.tos
endif
ST_TEST_OBJS = $(INTDIR)/st_tests_main.o $(INTDIR)/st_autotests.o $(INTDIR)/st_c2p_autotests.o $(INTDIR)/st_terrain_tile_left_clip_autotest.o $(INTDIR)/st16_convert_autotest.o $(INTDIR)/st_iconset_grid_interactive.o $(INTDIR)/st_interactive.o $(INTDIR)/st_blitter_rect_interactive.o $(INTDIR)/st_pcx_minimal.o $(INTDIR)/st_mix_minimal.o $(INTDIR)/st_text.o $(INTDIR)/st_title_production.o $(INTDIR)/st_game_link_stubs.o $(INTDIR)/st_build_frame_assets.o $(INTDIR)/st_font_browser.o $(INTDIR)/st_audio_asset_autotest.o $(INTDIR)/st_audio_mix_test.o $(INTDIR)/st_audx_autotest.o $(INTDIR)/st_mix_register.o $(INTDIR)/st_planar_line_bench.o $(INTDIR)/st_wsa_playback.o $(INTDIR)/st_cps_browser.o
ST_TEST_LINK_OBJS = $(INTDIR)/c2p.o $(INTDIR)/palette.o $(INTDIR)/const.o $(INTDIR)/st_temperat_palette_data.o $(INTDIR)/title_pcx_load_st_tests.o \
	$(INTDIR)/gbuffer.o $(INTDIR)/drawbuff.o $(INTDIR)/iconset_ATARILIB.o $(INTDIR)/st16_iconset.o $(INTDIR)/st16_convert.o $(INTDIR)/st16_draw.o $(INTDIR)/st_screen.o $(INTDIR)/st_blit.o $(INTDIR)/st_blitter_hw.o $(INTDIR)/st_soft_blit.o $(INTDIR)/st_sprite_cache.o $(INTDIR)/shpx.o \
	$(INTDIR)/st_planar_draw.o $(INTDIR)/st_hw_probe.o $(INTDIR)/st_cache.o $(INTDIR)/st_cache_ops.o \
	$(INTDIR)/ccfile.o $(INTDIR)/mixfile.o $(INTDIR)/link.o $(INTDIR)/newdel.o \
	$(INTDIR)/alloc_ATARILIB.o $(INTDIR)/rawfile.o $(INTDIR)/font.o \
	$(INTDIR)/cdfile.o $(INTDIR)/misc_ATARILIB.o $(INTDIR)/mouseww.o $(INTDIR)/getshape.o $(INTDIR)/iff.o \
	$(INTDIR)/keyboard.o $(INTDIR)/wwkeyboard_st.o $(INTDIR)/ikbd.o $(INTDIR)/keyframe.o $(INTDIR)/wsa.o \
	$(INTDIR)/audio_ste.o $(INTDIR)/ste_stream_pcm.o $(INTDIR)/ste_stream_ima99.o $(INTDIR)/ws_adpcm_68k.o \
	$(INTDIR)/audx_pool_file.o $(INTDIR)/audx_page_cache.o $(INTDIR)/ste_stream_memory_source.o \
	$(INTDIR)/ste_stream_cache_source.o $(INTDIR)/ste_stream_file_source.o \
	$(INTDIR)/st_load_uncompress.o \
	$(INTDIR)/buff.o $(INTDIR)/bfiofile.o $(INTDIR)/blwstraw.o $(INTDIR)/blowfish.o \
	$(INTDIR)/crc.o $(INTDIR)/fading.o $(INTDIR)/file.o $(INTDIR)/file_posix.o \
	$(INTDIR)/paths.o $(PATHS_IMPL_OBJ) $(INTDIR)/libcmini_posix_shim.o $(INTDIR)/debugstring.o \
	$(INTDIR)/pk.o $(INTDIR)/pkstraw.o $(INTDIR)/rndstraw.o $(INTDIR)/sha.o $(INTDIR)/shastraw.o \
	$(INTDIR)/straw.o $(INTDIR)/xstraw.o $(INTDIR)/wwkeyboard.o \
	$(INTDIR)/misc.o $(INTDIR)/random.o $(INTDIR)/int.o $(INTDIR)/mp.o

st-tests: $(ST_TEST_EXE)
	@echo "ST test suite: $(ST_TEST_EXE)"

$(ST_TEST_EXE): $(ST_TEST_OBJS) $(ST_TEST_LINK_OBJS) | $(OUTDIR) $(INTDIR)
	@echo "Linking $(ST_TEST_EXE)..."
	$(CXX) $(LINKFLAGS) -o $(ST_TEST_EXE) $(LINK_PRE_OBJS) $(ST_TEST_OBJS) $(ST_TEST_LINK_OBJS) $(LIBS)

$(INTDIR)/st_tests_main.o: $(ST_SUITE_DIR)/st_tests_main.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_autotests.o: $(ST_SUITE_DIR)/st_autotests.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_c2p_autotests.o: $(ST_SUITE_DIR)/st_c2p_autotests.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_terrain_tile_left_clip_autotest.o: $(ST_SUITE_DIR)/st_terrain_tile_left_clip_autotest.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st16_convert_autotest.o: $(ST_SUITE_DIR)/st16_convert_autotest.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_iconset_grid_interactive.o: $(ST_SUITE_DIR)/st_iconset_grid_interactive.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_interactive.o: $(ST_SUITE_DIR)/st_interactive.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_blitter_rect_interactive.o: $(ST_SUITE_DIR)/st_blitter_rect_interactive.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_pcx_minimal.o: $(ST_SUITE_DIR)/st_pcx_minimal.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_mix_minimal.o: $(ST_SUITE_DIR)/st_mix_minimal.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_text.o: $(ST_SUITE_DIR)/st_text.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_title_production.o: $(ST_SUITE_DIR)/st_title_production.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_game_link_stubs.o: $(ST_SUITE_DIR)/st_game_link_stubs.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_build_frame_assets.o: $(ST_SUITE_DIR)/st_build_frame_assets.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_font_browser.o: $(ST_SUITE_DIR)/st_font_browser.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_audio_asset_autotest.o: $(ST_SUITE_DIR)/st_audio_asset_autotest.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_audio_mix_test.o: $(ST_SUITE_DIR)/st_audio_mix_test.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_audx_autotest.o: $(ST_SUITE_DIR)/st_audx_autotest.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_mix_register.o: $(ST_SUITE_DIR)/st_mix_register.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_planar_line_bench.o: $(ST_SUITE_DIR)/st_planar_line_bench.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_wsa_playback.o: $(ST_SUITE_DIR)/st_wsa_playback.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_cps_browser.o: $(ST_SUITE_DIR)/st_cps_browser.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_load_uncompress.o: $(ST_SUITE_DIR)/st_load_uncompress.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/title_pcx_load_st_tests.o: ATARILIB/title_pcx_load.cpp | $(INTDIR)
	@echo "Compiling $< (st-tests)..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -DCNC_ST_TEST_EXE -o $@ -c $<
endif

#=============================================================================
# remix-web — browser MIX prep wizard (host build, static output)
#=============================================================================

remix-web:
	$(MAKE) -C tools/remix-web

remix-web-itch:
	$(MAKE) -C tools/remix-web itch

.PHONY: remix-web remix-web-itch

#=============================================================================
# itch.io release packages (Atari ST only)
#=============================================================================
ifeq ($(PLATFORM_LIB),ATARILIB)

GIT_ROOT = $(abspath ..)
NM = $(CROSS_PREFIX)/$(CROSS_TARGET)-nm
STRIP = $(CROSS_PREFIX)/$(CROSS_TARGET)-strip
ITCH_DIST_DIR = dist/itch
ITCH_STAGE_DIR = $(ITCH_DIST_DIR)/stage
ITCH_RELEASE_TOS = ./bin/AtariST/cnc.tos
ITCH_MAX_TOS_BYTES = 2097152
# Set ITCH_ALLOW_DIRTY=1 to skip the clean working-tree check (local/test builds only).
ITCH_ALLOW_DIRTY ?= 0

itch-release:
	@set -e; \
	echo "=== itch.io release ==="; \
	if [ "$(ITCH_ALLOW_DIRTY)" != "1" ] && [ -n "$$(git -C "$(GIT_ROOT)" status --porcelain)" ]; then \
		echo "error: repository is not clean (commit or stash changes first, or ITCH_ALLOW_DIRTY=1)" >&2; \
		exit 1; \
	fi; \
	if [ "$(ITCH_ALLOW_DIRTY)" = "1" ] && [ -n "$$(git -C "$(GIT_ROOT)" status --porcelain)" ]; then \
		echo "warning: ITCH_ALLOW_DIRTY=1 — proceeding with dirty working tree"; \
	fi; \
	RELEASE_TAG=$$(git -C "$(GIT_ROOT)" describe --exact-match --tags --match 'release/atarist/v*' HEAD 2>/dev/null || true); \
	if [ -z "$$RELEASE_TAG" ]; then \
		echo "error: HEAD must be tagged release/atarist/vx.y.z" >&2; \
		exit 1; \
	fi; \
	case "$$RELEASE_TAG" in \
		release/atarist/v[0-9]*.[0-9]*.[0-9]*) ;; \
		*) echo "error: tag must match release/atarist/vx.y.z (got $$RELEASE_TAG)" >&2; exit 1;; \
	esac; \
	VERSION=$${RELEASE_TAG#release/atarist/v}; \
	echo "Release tag: $$RELEASE_TAG (version $$VERSION)"; \
	$(MAKE) clean CONFIG=Release; \
	$(MAKE) all CONFIG=Release WITH_SYMBOLS=1 PROFILE_FRAME_BAR=1 OUTDIR=./bin/AtariST/; \
	if [ ! -f "$(ITCH_RELEASE_TOS)" ]; then \
		echo "error: $(ITCH_RELEASE_TOS) not found after build" >&2; \
		exit 1; \
	fi; \
	rm -rf "$(ITCH_DIST_DIR)"; \
	mkdir -p "$(ITCH_STAGE_DIR)"; \
	echo "Extracting Hatari-compatible symbols..."; \
	{ echo "# $$RELEASE_TAG — generated from cnc.tos for Hatari debugger/profiler"; \
	  "$(NM)" -n "$(ITCH_RELEASE_TOS)" | awk '$$2 ~ /^[TDB]$$/ {print $$1, $$2, $$3}'; } \
	  > "$(ITCH_DIST_DIR)/cnc.sym"; \
	cp "$(ITCH_RELEASE_TOS)" "$(ITCH_STAGE_DIR)/cnc.tos"; \
	echo "Stripping debug symbols from staged cnc.tos..."; \
	"$(STRIP)" "$(ITCH_STAGE_DIR)/cnc.tos"; \
	TOS_SIZE=$$(wc -c < "$(ITCH_STAGE_DIR)/cnc.tos" | tr -d ' '); \
	if [ "$$TOS_SIZE" -ge $(ITCH_MAX_TOS_BYTES) ]; then \
		echo "error: stripped cnc.tos is $$TOS_SIZE bytes (limit $(ITCH_MAX_TOS_BYTES))" >&2; \
		exit 1; \
	fi; \
	echo "Stripped cnc.tos: $$TOS_SIZE bytes"; \
	for f in atari-assets/*.w16; do \
		if [ -f "$$f" ]; then cp "$$f" "$(ITCH_STAGE_DIR)/"; fi; \
	done; \
	mkdir -p "$(ITCH_STAGE_DIR)/video"; \
	for f in atari-assets/video/*.w16; do \
		if [ -f "$$f" ]; then cp "$$f" "$(ITCH_STAGE_DIR)/video/"; fi; \
	done; \
	if [ ! -f atari-assets/record.bin ]; then \
		echo "error: atari-assets/record.bin not found" >&2; \
		exit 1; \
	fi; \
	cp atari-assets/record.bin "$(ITCH_STAGE_DIR)/"; \
	printf '%s\r\n' \
		"Command & Conquer for Atari ST" \
		"" \
		"Extract these files to a folder on your Atari ST hard drive (emulated or real)." \
		"" \
		"To play the game, copy the MIX files from an official Command & Conquer DOS CD into the same folder as the executable." \
		"Prepare MIX files for Atari ST with remix-web (see source tree) before running cnc.tos." \
		"" \
		"Playback benchmark: record.bin is included. Run cnc.tos -XY (or -XYQ for mostly silent audio)." \
		"" \
		"Further information:" \
		"https://indyjo.itch.io/commandconquer" \
		"" \
		"This game is provided under the terms of the GNU General Public License, version 3 (GPLv3)." \
		"" \
		"Source code:" \
		"https://github.com/indyjo/Vanilla-Conquer-Atari-ST" \
		> "$(ITCH_STAGE_DIR)/readme.txt"; \
	GAME_ZIP="$(ITCH_DIST_DIR)/cncst-$$VERSION.zip"; \
	SYM_ZIP="$(ITCH_DIST_DIR)/cncst-sym-$$VERSION.zip"; \
	rm -f "$$GAME_ZIP" "$$SYM_ZIP"; \
	( cd "$(ITCH_STAGE_DIR)" && zip -q -r "../cncst-$$VERSION.zip" . ); \
	( cd "$(ITCH_DIST_DIR)" && zip -q "cncst-sym-$$VERSION.zip" cnc.sym ); \
	echo "Created $$GAME_ZIP"; \
	echo "Created $$SYM_ZIP"

endif

#=============================================================================
# DEPENDENCIES
#=============================================================================

# Makefile toggles (CHEAT_KEYS, etc.) change -D gates; track them so stale .o files
# are not linked after e.g. make CHEAT_KEYS=1 on a Release tree.
BUILD_FLAGS_STAMP := $(INTDIR)/.build_flags

$(BUILD_FLAGS_STAMP): | $(INTDIR)
	@printf 'CHEAT_KEYS=%s\nPROFILE_FRAME_BAR=%s\nPROFILE_BORDER=%s\nSEPARATE_DEBUG_SCREEN=%s\n' \
		'$(CHEAT_KEYS)' '$(PROFILE_FRAME_BAR)' '$(PROFILE_BORDER)' '$(SEPARATE_DEBUG_SCREEN)' > '$@.tmp'
	@if ! cmp -s '$@.tmp' '$@' 2>/dev/null; then mv '$@.tmp' '$@'; else rm -f '$@.tmp'; fi

# defines.h and makefile DEFINES toggle CHEAT_KEYS and other #ifdef gates; stale .o files break the link.
$(CPP_OBJECTS): defines.h makefile $(BUILD_FLAGS_STAMP)

# Note: For automatic dependency generation, you can:
# 1. Add -MMD -MP to CXXFLAGS to generate .d files
# 2. Include those .d files here with: -include $(CPP_OBJECTS:.o=.d)
# 3. Example: CXXFLAGS += -MMD -MP
# 4. Then add: -include $(wildcard $(INTDIR)/*.d)
