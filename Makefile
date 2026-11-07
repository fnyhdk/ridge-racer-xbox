# Ridge Racer (SLPS-00001) for the original Xbox, built with nxdk.
#
#   make                 -> bin/default.xbe and RidgeRacer.iso (an XISO)
#   make CUE=path.cue    -> the same, with the disc's audio tracks (music)
#
# The game's code is translated from your own PSX.EXE by the decompilation's
# static recompiler; the disc image is served to it as the CD drive. Both come
# from your own disc and both are copied next to the XBE -- nothing here ships
# game data.

XBE_TITLE  = Ridge\ Racer
GEN_XISO   = RidgeRacer.iso
NXDK_SDL   = y

NXDK_DIR  ?= $(CURDIR)/../nxdk-master
DECOMP    ?= $(CURDIR)/../rr-decomp-0.1.0
PSX_EXE   ?= $(DECOMP)/PSX.EXE
DISC_ISO  ?= $(DECOMP)/SLPS-00001.iso
CUE       ?=

ifeq ($(filter clean%,$(MAKECMDGOALS)),)
ifeq ($(wildcard $(PSX_EXE)),)
$(error PSX.EXE not found at $(PSX_EXE): copy it from your disc, or pass PSX_EXE=...)
endif
ifeq ($(wildcard $(DISC_ISO)),)
$(error disc image not found at $(DISC_ISO): pass DISC_ISO=... (see README.md))
endif
endif

M0  = $(DECOMP)/tools/m0
GEN = $(CURDIR)/gen

# The runtime, compiled from the decompilation's own files. video.c is the
# desktop's window; src/video_xbox.c replaces it.
M0_SRCS = main.c hw.c gpu.c gte.c spu.c mods.c audio.c
GAME_SRCS = $(addprefix $(GEN)/,$(M0_SRCS) game.c table.c cdfiles.c) \
            $(CURDIR)/src/video_xbox.c $(CURDIR)/src/xbox_main.c
SRCS = $(GAME_SRCS)

include $(NXDK_DIR)/Makefile

GAME_OBJS = $(addsuffix .obj, $(basename $(GAME_SRCS)))

# Scoped to the game's objects: nxdk compiles its SDL with the same pattern
# rule, and the compat header has no business there.
# -fno-strict-aliasing: memory is an array of words read through narrower
# pointers (see rt.h).
$(GAME_OBJS): CFLAGS += -O2 -w -fcommon -fno-strict-aliasing -DWITH_SDL \
                        -I$(M0) -I$(CURDIR)/src/compat \
                        -include $(CURDIR)/src/compat/xbox_compat.h
$(GEN)/main.obj: CFLAGS += -Dmain=m0_main

# The runtime's files are copied rather than compiled in place, so that the
# objects land here and not in the decompilation's tree. An edit to tools/m0
# is picked up on the next make.
$(addprefix $(GEN)/,$(M0_SRCS)): $(GEN)/%.c: $(M0)/%.c
	@mkdir -p $(GEN)
	@cp $< $@

# The translated game. Regenerated when the executable or the translator
# changes; translating 949 functions takes a few seconds. A stamp stands for
# the three files: macOS's make predates grouped targets. The empty recipe
# makes make look at the files again once the stamp is made; without one it
# kept their old times, and linked the new translation only on the next run.
$(GEN)/game.c $(GEN)/table.c $(GEN)/cdfiles.c: $(GEN)/.stamp
	@true
$(GEN)/.stamp: $(PSX_EXE) $(CURDIR)/gen.py $(DECOMP)/tools/recomp.py $(M0)/build.py $(GEN)/cue.txt
	@echo "[ RECOMP   ] $(PSX_EXE)"
	$(VE)DECOMP=$(DECOMP) python3 $(CURDIR)/gen.py "$(PSX_EXE)" --iso "$(DISC_ISO)" \
	    $(if $(CUE),--cue "$(CUE)") --out $(GEN)
	$(if $(CUE),,@rm -rf $(OUTPUT_DIR)/tracks $(GEN)/split)
	@touch $@

# The sheet in use, rewritten only when it changes: building with or without
# CUE= must translate again, since the track list is compiled in.
$(GEN)/cue.txt: FORCE
	@mkdir -p $(GEN)
	@echo '$(CUE)' | cmp -s - $@ || echo '$(CUE)' > $@
.PHONY: FORCE
FORCE:

# The files the XBE reads at run time, on D: next to it.
$(OUTPUT_DIR)/PSX.EXE: $(PSX_EXE) | $(OUTPUT_DIR)
	@cp $< $@
$(OUTPUT_DIR)/disc.iso: $(DISC_ISO) | $(OUTPUT_DIR)
	@echo "[ DISC     ] $@"
	@cp -c $< $@ 2>/dev/null || cp $< $@
$(OUTPUT_DIR)/tracks: $(GEN)/cdfiles.c | $(OUTPUT_DIR)
	@rm -rf $@ && mkdir -p $@
	@while IFS="	" read -r src name; do cp "$$src" "$@/$$name"; done < $(GEN)/tracks.txt
	@touch $@

DATA = $(OUTPUT_DIR)/PSX.EXE $(OUTPUT_DIR)/disc.iso $(if $(CUE),$(OUTPUT_DIR)/tracks)
$(GEN_XISO): $(DATA)
all: $(DATA)

.PHONY: clean-game
clean-game:
	rm -rf $(GEN) $(OUTPUT_DIR) $(CURDIR)/src/*.obj $(CURDIR)/src/*.d main.exe $(GEN_XISO)
