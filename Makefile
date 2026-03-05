MODULE := codec_acelp_tetra.so
MOD_SRC := codec_acelp_tetra.c

ASTERISK_INCLUDE ?= /usr/include/asterisk
ASTERISK_SRC ?=
TETRA_CODEC_SRC ?=
MODULES_DIR ?= /usr/lib/asterisk/modules
CC ?= gcc
ETSI_OVERRIDE_DIR := $(CURDIR)/etsi_overrides

# Patched ETSI reference source files needed for in-process encode/decode.
TETRA_SRCS := \
	scod_tet.c \
	sdec_tet.c \
	sub_sc_d.c \
	sub_dsp.c \
	fbas_tet.c \
	fexp_tet.c \
	fmat_tet.c \
	tetra_op.c

ifneq ($(filter clean,$(MAKECMDGOALS)),clean)
ifeq ($(strip $(TETRA_CODEC_SRC)),)
$(error TETRA_CODEC_SRC must point to patched ETSI codec directory)
endif
endif

ifneq ($(ASTERISK_SRC),)
INCLUDE_FLAGS := -I$(ASTERISK_SRC)/include -I$(ASTERISK_SRC)
else
INCLUDE_FLAGS := -I$(ASTERISK_INCLUDE)
endif
INCLUDE_FLAGS += -I$(CURDIR) -I$(ETSI_OVERRIDE_DIR) -I$(TETRA_CODEC_SRC)

OBJS := codec_acelp_tetra.o $(TETRA_SRCS:.c=.o)

CFLAGS ?= -O2 -g
CFLAGS += -fPIC -fcommon -Wall -Wextra -D_GNU_SOURCE $(INCLUDE_FLAGS)
LDFLAGS ?= -shared -Wl,--allow-multiple-definition

.PHONY: all clean install

all: $(MODULE)

$(MODULE): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

codec_acelp_tetra.o: $(MOD_SRC) acelp_state_bridge.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(TETRA_SRCS:.c=.o):
	@src="$(TETRA_CODEC_SRC)/$*.c"; \
	if [ -f "$(ETSI_OVERRIDE_DIR)/$*.c" ]; then src="$(ETSI_OVERRIDE_DIR)/$*.c"; fi; \
	$(CC) $(CFLAGS) -c -o $@ "$$src"

install: $(MODULE)
	install -d $(DESTDIR)$(MODULES_DIR)
	install -m 755 $(MODULE) $(DESTDIR)$(MODULES_DIR)/$(MODULE)

clean:
	rm -f $(OBJS) $(MODULE)
