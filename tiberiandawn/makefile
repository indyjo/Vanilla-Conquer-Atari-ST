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
else
DEFINES += WIN32 _WINDOWS _USRDLL TIBERIANDAWN_EXPORTS
endif

# Include directories (platform-specific + common)
# Put platform lib first so platform-specific headers take precedence over root headers
INCLUDES = -I./$(PLATFORM_LIB) -I. -I./COMMONLIB -I.. -I../common

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

# Common compiler flags (GCC style)
CXXFLAGS_COMMON = -Wall -Wextra -fno-exceptions
CXXFLAGS_COMMON += $(INCLUDES)
CXXFLAGS_COMMON += -Wno-unused-parameter -Wno-sign-compare -Wno-deprecated-declarations
CXXFLAGS_COMMON += -std=c++17 -fpermissive
CXXFLAGS_COMMON += $(FASTCALL_FLAGS)

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

# Debug configuration
ifeq ($(CONFIG),Debug)
DEFINES += _DEBUG
CXXFLAGS = $(CXXFLAGS_COMMON) -O0 -g -DDEBUG
LDFLAGS = -g
LIBS = $(PLATFORM_LIBS)
else
# Release configuration
DEFINES += NDEBUG
# e.g. make CXX_OPT_REL=-O3 CONFIG=release ...
CXX_OPT_REL ?= -O3
WITH_SYMBOLS ?= 1
CXXFLAGS = $(CXXFLAGS_COMMON) $(CXX_OPT_REL) -DNDEBUG
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
ASMFLAGS = -m68000
ifeq ($(CONFIG),Debug)
ASMFLAGS += -g
endif
ifeq ($(WITH_SYMBOLS),1)
ASMFLAGS += -g
endif

# Resource compiler flags (windres style)
RCFLAGS = -O coff
ifeq ($(CONFIG),Debug)
RCFLAGS += -D_DEBUG=1
else
RCFLAGS += -DNDEBUG=1
endif

# Linker flags (GCC style)
# Platform-specific linking
ifeq ($(PLATFORM_LIB),ATARILIB)
# Atari ST: Build as TOS executable, not DLL
# Note: -nostdlib removed - we need standard library for TOS executables
# Entry point will be main() from startup.cpp
# No special entry point needed - standard C++ main() is fine
else
# Windows: Build as DLL
LDFLAGS += -shared
LDFLAGS += -Wl,-soname,$(TARGET_NAME).dll
endif

# Link driver flags: LDFLAGS plus ABI flags. Appended here (not via LDFLAGS +=) because
# Release WITH_SYMBOLS uses "override LDFLAGS :=" which drops a later LDFLAGS +=.
LINKFLAGS = $(LDFLAGS) $(FASTCALL_FLAGS)

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
	../common/paths_posix.cpp ../common/pipe.cpp ../common/pk.cpp \
	../common/pkpipe.cpp ../common/pkstraw.cpp ../common/random.cpp \
	../common/rawfile.cpp ../common/readline.cpp ../common/rgb.cpp \
	../common/rndstraw.cpp ../common/settings.cpp ../common/sha.cpp \
	../common/shapipe.cpp ../common/shastraw.cpp ../common/straw.cpp \
	../common/toggle.cpp ../common/timer.cpp ../common/timer_st_vbl.cpp ../common/timerdwn.cpp ../common/vector.cpp \
	../common/winasm.cpp ../common/wwkeyboard.cpp \
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
	UTRACKER.CPP VECTOR.CPP 	VISUDLG.CPP WINSTUB.CPP wsa.cpp \
	$(if $(filter ATARILIB,$(PLATFORM_LIB)),wsa_atari_weights.cpp theater_atari_weights.cpp,)

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
	$(PLATFORM_LIB)/drawbuff.cpp $(PLATFORM_LIB)/st_planar_draw.cpp $(PLATFORM_LIB)/st_blitter_blit.cpp $(PLATFORM_LIB)/st_sprite_cache.cpp $(PLATFORM_LIB)/font.cpp $(PLATFORM_LIB)/gbuffer.cpp \
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
	$(if $(filter ATARILIB,$(PLATFORM_LIB)),$(PLATFORM_LIB)/direct.cpp $(PLATFORM_LIB)/misc.cpp $(PLATFORM_LIB)/st_gitinfo.cpp $(PLATFORM_LIB)/st_link_stubs.cpp $(PLATFORM_LIB)/windows_time.cpp $(PLATFORM_LIB)/title_pcx_load.cpp $(PLATFORM_LIB)/audio_ste.cpp $(PLATFORM_LIB)/ste_stream_pcm.cpp $(PLATFORM_LIB)/ste_stream_ima99.cpp $(PLATFORM_LIB)/ws_adpcm_68k.cpp $(PLATFORM_LIB)/st_frame_meter.cpp $(PLATFORM_LIB)/st_screen.cpp,)

# Assembly sources (platform-specific)
# Root directory files use uppercase .ASM, ATARILIB files use lowercase .asm (as tracked in git)
ASM_SOURCES = \
	IPXPROT.ASM IPXREAL.ASM KEYFBUFF.ASM MMX.ASM PAGFAULT.ASM \
	SUPPORT.ASM TXTPRNT.ASM WINASM.ASM

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
# misc.cpp exists in ../common/ (VC_COMMON) and ATARILIB/ — keep both objects
PLATFORM_LIB_CPP_OBJS := $(subst $(INTDIR)/misc.o,$(INTDIR)/misc_$(PLATFORM_LIB).o,$(PLATFORM_LIB_CPP_OBJS))
# gadget.cpp exists in ../common/ (VC_COMMON) and root GADGET.CPP — root excluded on ST
CPP_OBJECTS = $(COMMONLIB_CPP_OBJS) $(VC_COMMON_CPP_OBJS) $(ROOT_CPP_OBJS) $(PLATFORM_LIB_CPP_OBJS)
ASM_OBJECTS = $(addprefix $(INTDIR)/,$(notdir $(patsubst %.ASM,%.o,$(patsubst %.asm,%.o,$(ASM_SOURCES)))))
# Filter out truncated x86 assembly object files for ATARILIB (they can't be assembled for m68k)
ifeq ($(PLATFORM_LIB),ATARILIB)
ASM_OBJECTS := $(filter-out $(INTDIR)/IPXPROT.o $(INTDIR)/IPXREAL.o $(INTDIR)/KEYFBUFF.o $(INTDIR)/MMX.o $(INTDIR)/PAGFAULT.o $(INTDIR)/SUPPORT.o $(INTDIR)/TXTPRNT.o $(INTDIR)/WINASM.o,$(ASM_OBJECTS))
endif
RC_OBJECTS = $(addprefix $(INTDIR)/,$(notdir $(RC_SOURCES:.rc=.o)))
# Filter out resource object files for ATARILIB (windres not available/needed for m68k)
ifeq ($(PLATFORM_LIB),ATARILIB)
RC_OBJECTS :=
endif

ALL_OBJECTS = $(CPP_OBJECTS) $(ASM_OBJECTS) $(RC_OBJECTS)

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

.PHONY: all clean cleanall help st-tests

# Default target
all: $(TARGET_EXE)
	@echo "Build complete: $(TARGET_EXE)"

# Main executable/DLL target
ifeq ($(PLATFORM_LIB),ATARILIB)
# Atari ST: Build TOS executable
$(TARGET_EXE): $(ALL_OBJECTS) | $(OUTDIR) $(INTDIR)
	@echo "Linking $(TARGET_EXE)..."
	$(CXX) $(LINKFLAGS) -o $(TARGET_EXE) $(ALL_OBJECTS) $(LIBS)
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
	@rm -f $(INTDIR)/st_tests_main.o $(INTDIR)/st_autotests.o $(INTDIR)/st_c2p_autotests.o $(INTDIR)/st_interactive.o $(INTDIR)/st_blitter_interactive.o $(INTDIR)/st_pcx_minimal.o $(INTDIR)/st_mix_minimal.o $(INTDIR)/st_text.o $(INTDIR)/st_title_production.o $(INTDIR)/st_title_menu_overlay.o $(INTDIR)/st_title_mouse_cursor.o $(INTDIR)/st_build_frame_assets.o $(INTDIR)/st_font_browser.o $(INTDIR)/st_game_link_stubs.o $(INTDIR)/st_audio_asset_autotest.o $(INTDIR)/st_cps_browser.o $(INTDIR)/title_pcx_load_st_tests.o $(INTDIR)/st_temperat_palette_data.o $(INTDIR)/st_blitter_blit.o

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
	@echo "  all      - Build the TOS executable (default)"
	@echo "  st-tests - Build tstcnc(.d).tos (on-machine / interactive suite)"
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
	@echo "  CXX_OPT_REL=-O3  - Release only: optimization flag (default: -O3)"
	@echo "  FASTCALL=0       - Disable -mfastcall on compile and link (default: 1)"
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
	@echo "  make USE_ATARILIB=1 CONFIG=release CXX_OPT_REL=-O3 WITH_SYMBOLS=1 PROFILE_FRAME_BAR=1 OUTDIR=./bin/AtariST/ -j4"
	@echo "  make PROFILE_FRAME_BAR=0 - Atari build without tab-strip profiler (on by default)"
	@echo "  make PROFILE_BORDER=0    - Audio VBL/IMA decode border timing (on by default)"
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
		echo "  - Linking with MiNT libraries (-lm -lc)"; \
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
ST_TEST_OBJS = $(INTDIR)/st_tests_main.o $(INTDIR)/st_autotests.o $(INTDIR)/st_c2p_autotests.o $(INTDIR)/st_interactive.o $(INTDIR)/st_blitter_interactive.o $(INTDIR)/st_pcx_minimal.o $(INTDIR)/st_mix_minimal.o $(INTDIR)/st_text.o $(INTDIR)/st_title_production.o $(INTDIR)/st_title_menu_overlay.o $(INTDIR)/st_title_mouse_cursor.o $(INTDIR)/st_game_link_stubs.o $(INTDIR)/st_build_frame_assets.o $(INTDIR)/st_font_browser.o $(INTDIR)/st_audio_asset_autotest.o $(INTDIR)/st_audio_mix_test.o $(INTDIR)/st_mix_register.o $(INTDIR)/st_planar_line_bench.o $(INTDIR)/st_wsa_playback.o $(INTDIR)/st_cps_browser.o
ST_TEST_LINK_OBJS = $(INTDIR)/c2p.o $(INTDIR)/palette.o $(INTDIR)/CONST.o $(INTDIR)/st_temperat_palette_data.o $(INTDIR)/title_pcx_load_st_tests.o \
	$(INTDIR)/gbuffer.o $(INTDIR)/drawbuff.o $(INTDIR)/st_screen.o $(INTDIR)/st_blitter_blit.o $(INTDIR)/st_sprite_cache.o \
	$(INTDIR)/st_planar_draw.o \
	$(INTDIR)/ccfile.o $(INTDIR)/mixfile.o $(INTDIR)/LINK.o $(INTDIR)/newdel.o \
	$(INTDIR)/alloc_ATARILIB.o $(INTDIR)/rawfile.o $(INTDIR)/font.o \
	$(INTDIR)/cdfile.o $(INTDIR)/misc_ATARILIB.o $(INTDIR)/mouseww.o $(INTDIR)/getshape.o $(INTDIR)/iff.o \
	$(INTDIR)/keyboard.o $(INTDIR)/wwkeyboard_st.o $(INTDIR)/ikbd.o $(INTDIR)/KEYFRAME.o $(INTDIR)/wsa.o $(INTDIR)/wsa_atari_weights.o \
	$(INTDIR)/audio_ste.o $(INTDIR)/ste_stream_pcm.o $(INTDIR)/ste_stream_ima99.o $(INTDIR)/ws_adpcm_68k.o \
	$(INTDIR)/st_load_uncompress.o \
	$(INTDIR)/buff.o $(INTDIR)/bfiofile.o $(INTDIR)/blwstraw.o $(INTDIR)/blowfish.o \
	$(INTDIR)/crc.o $(INTDIR)/fading.o $(INTDIR)/file.o $(INTDIR)/file_posix.o \
	$(INTDIR)/paths.o $(INTDIR)/paths_posix.o $(INTDIR)/debugstring.o \
	$(INTDIR)/pk.o $(INTDIR)/pkstraw.o $(INTDIR)/rndstraw.o $(INTDIR)/sha.o $(INTDIR)/shastraw.o \
	$(INTDIR)/straw.o $(INTDIR)/xstraw.o $(INTDIR)/wwkeyboard.o \
	$(INTDIR)/misc.o $(INTDIR)/random.o $(INTDIR)/int.o $(INTDIR)/mp.o

st-tests: $(ST_TEST_EXE)
	@echo "ST test suite: $(ST_TEST_EXE)"

$(ST_TEST_EXE): $(ST_TEST_OBJS) $(ST_TEST_LINK_OBJS) | $(OUTDIR) $(INTDIR)
	@echo "Linking $(ST_TEST_EXE)..."
	$(CXX) $(LINKFLAGS) -o $(ST_TEST_EXE) $(ST_TEST_OBJS) $(ST_TEST_LINK_OBJS) $(LIBS)

$(INTDIR)/st_tests_main.o: $(ST_SUITE_DIR)/st_tests_main.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_autotests.o: $(ST_SUITE_DIR)/st_autotests.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_c2p_autotests.o: $(ST_SUITE_DIR)/st_c2p_autotests.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_interactive.o: $(ST_SUITE_DIR)/st_interactive.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_blitter_interactive.o: $(ST_SUITE_DIR)/st_blitter_interactive.cpp | $(INTDIR)
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

$(INTDIR)/st_title_menu_overlay.o: $(ST_SUITE_DIR)/st_title_menu_overlay.cpp | $(INTDIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

$(INTDIR)/st_title_mouse_cursor.o: $(ST_SUITE_DIR)/st_title_mouse_cursor.cpp | $(INTDIR)
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
# DEPENDENCIES
#=============================================================================

# Note: For automatic dependency generation, you can:
# 1. Add -MMD -MP to CXXFLAGS to generate .d files
# 2. Include those .d files here with: -include $(CPP_OBJECTS:.o=.d)
# 3. Example: CXXFLAGS += -MMD -MP
# 4. Then add: -include $(wildcard $(INTDIR)/*.d)
