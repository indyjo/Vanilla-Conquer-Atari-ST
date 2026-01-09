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
OUTDIR ?= ../bin/$(PLATFORM)
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
DEFINES += POSIX BIG_ENDIAN
else
DEFINES += WIN32 _WINDOWS _USRDLL TIBERIANDAWN_EXPORTS
endif

# Include directories (platform-specific + common)
# Put platform lib first so platform-specific headers take precedence over root headers
INCLUDES = -I./$(PLATFORM_LIB) -I. -I./COMMONLIB

# Common compiler flags (GCC style)
CXXFLAGS_COMMON = -Wall -Wextra -fno-exceptions -fpack-struct=1
CXXFLAGS_COMMON += $(INCLUDES)
CXXFLAGS_COMMON += -Wno-unused-parameter -Wno-sign-compare -Wno-deprecated-declarations
CXXFLAGS_COMMON += -std=c++17 -fpermissive

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
CXXFLAGS = $(CXXFLAGS_COMMON) -O2 -DNDEBUG
# Note: -ffunction-sections and -fdata-sections may not be supported for m68k
# CXXFLAGS += -ffunction-sections -fdata-sections
LDFLAGS = -s
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
# Generate import library (may not be supported on all targets)
# Will be handled separately if needed

#=============================================================================
# SOURCE FILES
#=============================================================================

# Common library C++ sources (platform-agnostic)
COMMONLIB_CPP_SOURCES = \
	COMMONLIB/coorda.cpp

# IPX sources - ipx95.cpp is used for all modern builds
# ipx.cpp was removed as it only contained DOS/DPMI-specific code
IPX_SOURCES = ipx95.cpp

# Root directory C++ sources
# Note: dde.cpp and ccdde.cpp are Windows-only (DDE is Windows-specific)
ROOT_CPP_SOURCES = \
	aadata.cpp abstract.cpp adata.cpp aircraft.cpp alloc.cpp anim.cpp \
	audio.cpp base.cpp bbdata.cpp bdata.cpp building.cpp bullet.cpp \
	cargo.cpp $(if $(filter WIN32LIB,$(PLATFORM_LIB)),ccdde.cpp,) ccfile.cpp cdata.cpp cdfile.cpp cell.cpp \
	checkbox.cpp cheklist.cpp colrlist.cpp combat.cpp combuf.cpp \
	comqueue.cpp confdlg.cpp connect.cpp conquer.cpp const.cpp \
	control.cpp coord.cpp credits.cpp crew.cpp $(if $(filter WIN32LIB,$(PLATFORM_LIB)),dde.cpp,) debug.cpp \
	descdlg.cpp dial8.cpp dialog.cpp display.cpp door.cpp dpmi.cpp drive.cpp edit.cpp \
	ending.cpp event.cpp expand.cpp $(if $(filter WIN32LIB,$(PLATFORM_LIB)),facing.cpp,) factory.cpp field.cpp \
	findpath.cpp flasher.cpp fly.cpp foot.cpp fuse.cpp gadget.cpp \
	gamedlg.cpp gauge.cpp globals.cpp goptions.cpp gscreen.cpp \
	hdata.cpp heap.cpp help.cpp house.cpp idata.cpp infantry.cpp \
	ini.cpp init.cpp internet.cpp interpal.cpp intro.cpp iomap.cpp \
	ioobj.cpp $(IPX_SOURCES) ipxaddr.cpp ipxconn.cpp ipxgconn.cpp \
	ipxmgr.cpp jshell.cpp keyframe.cpp layer.cpp link.cpp list.cpp \
	loaddlg.cpp logic.cpp map.cpp mapeddlg.cpp mapedit.cpp mapedplc.cpp \
	mapedsel.cpp mapedtm.cpp mapsel.cpp menus.cpp \
	mission.cpp mixfile.cpp monoc.cpp mouse.cpp mplayer.cpp \
	msgbox.cpp msglist.cpp netdlg.cpp noseqcon.cpp nullconn.cpp \
	nulldlg.cpp nullmgr.cpp object.cpp odata.cpp options.cpp \
	overlay.cpp packet.cpp power.cpp profile.cpp queue.cpp radar.cpp \
	radio.cpp rand.cpp rawfile.cpp reinf.cpp rules.cpp saveload.cpp \
	scenario.cpp score.cpp scroll.cpp sdata.cpp seqconn.cpp Shape.cpp \
	shapebtn.cpp sidebar.cpp SIDEBARGlyphx.CPP slider.cpp smudge.cpp \
	sounddlg.cpp special.cpp stats.cpp super.cpp tab.cpp \
	tarcom.cpp target.cpp tdata.cpp team.cpp teamtype.cpp \
	techno.cpp template.cpp terrain.cpp textbtn.cpp theme.cpp \
	toggle.cpp trigger.cpp turret.cpp txtlabel.cpp udata.cpp unit.cpp \
	utracker.cpp vector.cpp visudlg.cpp winstub.cpp

# Platform library C++ sources (WIN32LIB or ATARILIB)
PLATFORM_LIB_CPP_SOURCES = \
	$(PLATFORM_LIB)/alloc.cpp $(PLATFORM_LIB)/buffer.cpp $(PLATFORM_LIB)/buffglbl.cpp \
	$(PLATFORM_LIB)/ddraw.cpp $(PLATFORM_LIB)/dipthong.cpp $(PLATFORM_LIB)/DrawMisc.cpp \
	$(PLATFORM_LIB)/drawbuff.cpp $(PLATFORM_LIB)/drawrect.cpp $(PLATFORM_LIB)/font.cpp $(PLATFORM_LIB)/gbuffer.cpp \
	$(PLATFORM_LIB)/getshape.cpp $(PLATFORM_LIB)/iconset.cpp $(PLATFORM_LIB)/iff.cpp \
	$(PLATFORM_LIB)/irandom.cpp $(PLATFORM_LIB)/keyboard.cpp $(PLATFORM_LIB)/load.cpp \
	$(PLATFORM_LIB)/loadfont.cpp $(PLATFORM_LIB)/loadpal.cpp $(PLATFORM_LIB)/mem.cpp \
	$(PLATFORM_LIB)/morphpal.cpp $(PLATFORM_LIB)/mouseww.cpp $(PLATFORM_LIB)/newdel.cpp \
	$(PLATFORM_LIB)/palette.cpp $(PLATFORM_LIB)/regionsz.cpp $(PLATFORM_LIB)/set_font.cpp \
	$(PLATFORM_LIB)/timer.cpp $(PLATFORM_LIB)/timerdwn.cpp $(PLATFORM_LIB)/timerini.cpp \
	$(if $(filter WIN32LIB,$(PLATFORM_LIB)),$(PLATFORM_LIB)/windows.cpp,) $(PLATFORM_LIB)/winhide.cpp $(PLATFORM_LIB)/writepcx.cpp \
	$(PLATFORM_LIB)/startup.cpp \
	$(if $(filter WIN32LIB,$(PLATFORM_LIB)),$(PLATFORM_LIB)/MiscAsm.cpp,) \
	$(if $(filter WIN32LIB,$(PLATFORM_LIB)),$(PLATFORM_LIB)/tcpip.cpp,) \
	$(if $(filter WIN32LIB,$(PLATFORM_LIB)),$(PLATFORM_LIB)/DLLInterface.cpp,) \
	$(if $(filter ATARILIB,$(PLATFORM_LIB)),$(PLATFORM_LIB)/direct.cpp $(PLATFORM_LIB)/facing.cpp $(PLATFORM_LIB)/facingclass.cpp $(PLATFORM_LIB)/misc.cpp $(PLATFORM_LIB)/windows_time.cpp,) \
	$(PLATFORM_LIB)/wsa.cpp $(PLATFORM_LIB)/_diptabl.cpp

# Assembly sources (platform-specific)
ASM_SOURCES = \
	ipxprot.asm ipxreal.asm keyfbuff.asm mmx.asm pagfault.asm \
	support.asm txtprnt.asm winasm.asm \
	$(PLATFORM_LIB)/facingff.asm $(PLATFORM_LIB)/tobuff.asm $(PLATFORM_LIB)/xordelta.asm

# Resource files
RC_SOURCES = \
	RESOURCE/TiberianDawn.rc

# All C++ sources
ALL_CPP_SOURCES = $(COMMONLIB_CPP_SOURCES) $(ROOT_CPP_SOURCES) $(PLATFORM_LIB_CPP_SOURCES)

# Generate object file lists (GCC uses .o extension)
# All object files go to $(INTDIR) with just the basename
# NOTE: alloc.cpp exists in both root and platform lib. The project uses both.
# We'll use unique names: alloc.o for root, alloc_platformlib.o for platform lib
# Handle both .cpp and .CPP extensions
COMMONLIB_CPP_OBJS = $(addprefix $(INTDIR)/,$(notdir $(COMMONLIB_CPP_SOURCES:.cpp=.o)))
ROOT_CPP_OBJS_TMP = $(addprefix $(INTDIR)/,$(notdir $(ROOT_CPP_SOURCES:.cpp=.o)))
ROOT_CPP_OBJS = $(ROOT_CPP_OBJS_TMP:.CPP=.o)
PLATFORM_LIB_CPP_OBJS = $(addprefix $(INTDIR)/,$(notdir $(PLATFORM_LIB_CPP_SOURCES:.cpp=.o)))
# Handle alloc.cpp conflict: root version becomes alloc.o, platform lib version becomes alloc_platformlib.o
PLATFORM_LIB_CPP_OBJS := $(subst $(INTDIR)/alloc.o,$(INTDIR)/alloc_$(PLATFORM_LIB).o,$(PLATFORM_LIB_CPP_OBJS))
# Handle facing.cpp conflict: ATARILIB version becomes facing_ATARILIB.o to avoid conflict with root facing.cpp
PLATFORM_LIB_CPP_OBJS := $(subst $(INTDIR)/facing.o,$(INTDIR)/facing_$(PLATFORM_LIB).o,$(PLATFORM_LIB_CPP_OBJS))
CPP_OBJECTS = $(COMMONLIB_CPP_OBJS) $(ROOT_CPP_OBJS) $(PLATFORM_LIB_CPP_OBJS)
ASM_OBJECTS = $(addprefix $(INTDIR)/,$(notdir $(ASM_SOURCES:.asm=.o)))
# Filter out truncated x86 assembly object files for ATARILIB (they can't be assembled for m68k)
ifeq ($(PLATFORM_LIB),ATARILIB)
ASM_OBJECTS := $(filter-out $(INTDIR)/ipxprot.o $(INTDIR)/ipxreal.o $(INTDIR)/keyfbuff.o $(INTDIR)/mmx.o $(INTDIR)/pagfault.o $(INTDIR)/support.o $(INTDIR)/txtprnt.o $(INTDIR)/winasm.o,$(ASM_OBJECTS))
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

.PHONY: all clean cleanall help

# Default target
all: $(TARGET_EXE)
	@echo "Build complete: $(TARGET_EXE)"

# Main executable/DLL target
ifeq ($(PLATFORM_LIB),ATARILIB)
# Atari ST: Build TOS executable
$(TARGET_EXE): $(ALL_OBJECTS) | $(OUTDIR) $(INTDIR)
	@echo "Linking $(TARGET_EXE)..."
	$(CXX) $(LDFLAGS) -o $(TARGET_EXE) $(ALL_OBJECTS) $(LIBS)
	@echo "TOS executable created: $(TARGET_EXE)"
else
# Windows: Build DLL
$(TARGET_DLL): $(ALL_OBJECTS) | $(OUTDIR)
	@echo "Linking $(TARGET_DLL)..."
	$(CXX) $(LDFLAGS) -o $(TARGET_DLL) $(ALL_OBJECTS) $(LIBS)
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
$(INTDIR)/%.o: COMMONLIB/%.cpp | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

# C++ compilation rules for root directory files
$(INTDIR)/%.o: %.cpp | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

# C++ compilation rules for root directory files with .CPP extension
$(INTDIR)/%.o: %.CPP | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

# C++ compilation rules for platform library files
$(INTDIR)/%.o: $(PLATFORM_LIB)/%.cpp | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

# Special rule for platform lib alloc.cpp to avoid conflict with root alloc.cpp
$(INTDIR)/alloc_$(PLATFORM_LIB).o: $(PLATFORM_LIB)/alloc.cpp | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

# Special rule for ATARILIB facing.cpp to avoid conflict with root facing.cpp
$(INTDIR)/facing_ATARILIB.o: ATARILIB/facing.cpp | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

# Explicit rule for mixfile.cpp to work around pattern matching issue with path separators in INTDIR
$(INTDIR)/mixfile.o: mixfile.cpp | $(INTDIR)
	@echo "Compiling $<..."
	@mkdir -p $(INTDIR)
	$(CXX) $(CXXFLAGS) $(DEFINE_FLAGS) -o $@ -c $<

# Assembly compilation rules for root directory files
# WARNING: These assembly files are x86-specific and will NOT work with m68k assembler
# You will need to either:
# 1. Convert them to m68k assembly, or
# 2. Rewrite the functions in C/C++, or
# 3. Skip them if not critical
$(INTDIR)/%.o: %.asm | $(INTDIR)
	@echo "Assembling $< (WARNING: x86 assembly on m68k target)..."
	@mkdir -p $(INTDIR)
	@echo "Error: x86 assembly files cannot be assembled for m68k target"
	@echo "Skipping $< - you may need to convert or rewrite in C/C++"
	@touch $@

# Assembly compilation rules for platform library files
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
else
	@echo "  all      - Build the DLL (default)"
endif
	@echo "  clean    - Remove build artifacts for current configuration"
	@echo "  cleanall - Remove all build artifacts"
	@echo "  help     - Show this help message"
	@echo ""
	@echo "Configuration Variables:"
	@echo "  CONFIG=Debug   - Build debug version (default: Release)"
	@echo "  CONFIG=Release - Build release version"
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
# DEPENDENCIES
#=============================================================================

# Note: For automatic dependency generation, you can:
# 1. Add -MMD -MP to CXXFLAGS to generate .d files
# 2. Include those .d files here with: -include $(CPP_OBJECTS:.o=.d)
# 3. Example: CXXFLAGS += -MMD -MP
# 4. Then add: -include $(wildcard $(INTDIR)/*.d)
