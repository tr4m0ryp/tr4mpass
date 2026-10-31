CC       = cc
CFLAGS   = -Wall -Wextra -std=c99 -D_GNU_SOURCE -O2 -MMD -MP
LDFLAGS  =

# Library discovery via pkg-config (warn but do not fail if missing)
PKG_LIBS = libimobiledevice-1.0 libirecovery-1.0 libusb-1.0 \
           libplist-2.0 openssl libcurl libssh2

CFLAGS  += $(shell pkg-config --cflags $(PKG_LIBS) 2>/dev/null)
LDFLAGS += $(shell pkg-config --libs   $(PKG_LIBS) 2>/dev/null)

$(foreach lib,$(PKG_LIBS),$(if $(shell pkg-config --exists $(lib) 2>/dev/null && echo ok),,$(warning Library $(lib) not found by pkg-config)))

# ------------------------------------------------------------------ #
# Feature probes for optional library symbols.                       #
#                                                                    #
# libplist 2.3 added plist_mem_free() and a 4-arg plist_from_memory. #
# Older libirecovery lacked IRECV_SEND_OPT_DFU_NOTIFY_FINISH.        #
# We probe by compiling a canary; on success we emit -D flags so the #
# include/compat/*.h shims pick the right branch. Results are also   #
# written to .build-flags for `tr4mpass doctor` (task 005) to        #
# surface at runtime.                                                #
# ------------------------------------------------------------------ #

PROBE_CFLAGS := $(shell pkg-config --cflags libplist-2.0 libirecovery-1.0 2>/dev/null)

# `#` is a make comment even inside $(shell ...), so emit the `#include`
# via printf's octal escape (\043).
HAVE_PLIST_MEM_FREE  := $(shell printf '\043include <plist/plist.h>\nint main(void){plist_mem_free((void*)0);return 0;}\n' | $(CC) $(PROBE_CFLAGS) -x c -c - -o /dev/null 2>/dev/null && echo 1 || echo 0)
HAVE_PLIST_FROM_MEM4 := $(shell printf '\043include <plist/plist.h>\nint main(void){plist_t p=0; plist_from_memory("",0,&p,(plist_format_t*)0);return 0;}\n' | $(CC) $(PROBE_CFLAGS) -x c -c - -o /dev/null 2>/dev/null && echo 1 || echo 0)
HAVE_IRECV_NOTIFY    := $(shell printf '\043include <libirecovery.h>\nint main(void){return (int)IRECV_SEND_OPT_DFU_NOTIFY_FINISH;}\n' | $(CC) $(PROBE_CFLAGS) -x c -c - -o /dev/null 2>/dev/null && echo 1 || echo 0)

ifeq ($(HAVE_PLIST_MEM_FREE),1)
    CFLAGS += -DTP_PLIST_HAS_MEM_FREE=1
endif
ifeq ($(HAVE_PLIST_FROM_MEM4),1)
    CFLAGS += -DTP_PLIST_HAS_FMT_ARG=1
endif
ifeq ($(HAVE_IRECV_NOTIFY),1)
    CFLAGS += -DTP_IRECV_HAS_NOTIFY_FINISH=1
endif

# Auto-discover all C sources under src/
SRCS     = $(shell find src -name '*.c')
OBJS     = $(SRCS:.c=.o)
TARGET   = tr4mpass

all: .build-flags $(TARGET)

$(TARGET): .chip-db-canary $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -Iinclude -c -o $@ $<

# .build-flags is a diagnostic file consumed by `tr4mpass doctor`.
# It records which optional symbols the probes detected on this host.
.build-flags:
	@printf 'HAVE_PLIST_MEM_FREE=%s\n'      '$(HAVE_PLIST_MEM_FREE)'  >  $@
	@printf 'HAVE_PLIST_FROM_MEM4=%s\n'     '$(HAVE_PLIST_FROM_MEM4)' >> $@
	@printf 'HAVE_IRECV_NOTIFY_FINISH=%s\n' '$(HAVE_IRECV_NOTIFY)'    >> $@

clean:
	find src -name '*.o' -delete
	rm -f $(TARGET) $(TEST_TARGET) $(MOCK_TARGET) .build-flags $(CHIP_DB_CANARY_BIN)
	find tests -name '*.o' -delete 2>/dev/null || true
	find src -name '*.d' -delete
	find tests -name '*.d' -delete 2>/dev/null || true

# ------------------------------------------------------------------ #
# Chip DB integrity canary (T5, issue #53).                          #
#                                                                    #
# Rebuilt and re-run on every $(TARGET) build. If any well-known     #
# row in include/device/chip_db_table*.h diverges from tests/        #
# chip_db_canary.c's expected (name, marketing, checkm8) tuple, the  #
# probe exits non-zero and the whole build fails -- guarding against #
# a repeat of the 0x8947/0x8960 mislabel bug where the CLI announced #
# the wrong device family. Bypass with STRICT_CHIP_DB=0 only when a  #
# distro-packaged chip table has legitimately diverged.              #
# ------------------------------------------------------------------ #
STRICT_CHIP_DB    ?= 1
CHIP_DB_CANARY_BIN = tests/chip_db_canary.bin

# Phony by design: the canary never produces a file named .chip-db-canary,
# so header prerequisites here would be dead. It re-runs on every build.
.chip-db-canary:
	@$(CC) $(CFLAGS) -Iinclude \
	    -DSTRICT_CHIP_DB=$(STRICT_CHIP_DB) \
	    tests/chip_db_canary.c -o $(CHIP_DB_CANARY_BIN)
	@./$(CHIP_DB_CANARY_BIN)

.PHONY: all clean test test-mocks .chip-db-canary .build-flags

# ------------------------------------------------------------------ #
# Test target: compile only hardware-independent units                #
# No mocks, no real library linkage required for the SUT code.        #
# ------------------------------------------------------------------ #

TEST_UNITS  = src/device/chip_db.c src/bypass/bypass.c src/util/log.c \
              src/util/env_config.c

# Sections linked by both `make test` (hw-independent) and `make test-mocks`.
TEST_SECT_CORE = tests/test_main.c tests/test_parsing.c tests/test_chip_db.c \
                 tests/test_bypass_probe.c tests/test_constants.c \
                 tests/test_cert_extract.c tests/test_signer.c \
                 tests/test_env_config.c

# Sections that require the mocked hardware libraries -- only used by
# `make test-mocks`.  Include stub definitions of their entry points in
# TEST_SECT_STUBS so the hw-independent target still links.
TEST_SECT_MOCK = tests/test_ramdisk.c tests/test_ssh_jailbreak.c

# Aggregate for the mock build: real section bodies.
TEST_SECT     = $(TEST_SECT_CORE) $(TEST_SECT_MOCK)
TEST_TARGET   = tests/run_tests
TEST_CFLAGS   = $(CFLAGS) -Iinclude -Itests -DUNIT_TEST

test: $(TEST_TARGET)
	./$(TEST_TARGET)

# The hw-independent target replaces the mock-only sections with empty
# stubs so run_tests still finds run_ramdisk_tests / run_ssh_jailbreak_tests.
$(TEST_TARGET): $(TEST_SECT_CORE) $(TEST_UNITS) tests/mock_section_stubs.c
	$(CC) $(TEST_CFLAGS) -o $@ $^

# ------------------------------------------------------------------ #
# Mock test target: full suite against mocked hardware libraries.     #
#                                                                    #
# Links the test sections + all tests/mocks/*.c + the production     #
# sources we want to exercise (everything under src/ except main.c   #
# and the hardware-specific checkm8/dfu exploit code).  The real     #
# hardware libs (libimobiledevice/libirecovery/libusb/libssh2/curl)  #
# are NOT linked -- the mocks satisfy those symbols.  libplist,      #
# openssl, and pthread remain real.                                  #
# ------------------------------------------------------------------ #

MOCK_SRCS    = $(shell find tests/mocks -name '*.c')
# Phase 3: pick up end-to-end integration test sources.  `find` is
# guarded so `make` still works before the directory exists.
INT_SRCS      = $(shell find tests/integration -name '*.c' 2>/dev/null)
# Production sources to include in the mock build.  We exclude:
#   - src/main.c (has its own main())
#   - src/exploit/* and src/device/usb_dfu.c (direct DFU/checkm8 h/w code)
MOCK_SUT_SRCS = $(shell find src -name '*.c' \
                  -not -path 'src/main.c' \
                  -not -path 'src/exploit/*' \
                  -not -path 'src/device/usb_dfu.c')
MOCK_ALL_SRCS = $(TEST_SECT) $(MOCK_SRCS) $(INT_SRCS) $(MOCK_SUT_SRCS)
MOCK_TARGET   = tests/run_mock_tests

# Only link libs that the SUT still needs at runtime and we did NOT mock.
MOCK_PKG_LIBS = libplist-2.0 openssl
# Headers-only pkg-config libs: symbols are mocked so we do NOT link them,
# but SUT sources still #include their public headers, so we need the
# include paths at compile time. The pkg-config --cflags output adds the
# libusb-1.0/ subdirectory, so `#include <libusb.h>` (include/device/device.h)
# resolves, but `#include <libusb-1.0/libusb.h>` needs the include root,
# which the explicit -I below provides.
MOCK_HEADER_LIBS = libusb-1.0 libimobiledevice-1.0 libirecovery-1.0 libssh2 libcurl
MOCK_CFLAGS   = -Wall -Wextra -std=c99 -D_GNU_SOURCE -O2 \
                $(shell pkg-config --cflags $(MOCK_PKG_LIBS) $(MOCK_HEADER_LIBS) 2>/dev/null) \
                -I$(shell pkg-config --variable=includedir libusb-1.0 2>/dev/null) \
                -Iinclude -Itests -Itests/integration -Itests/mocks \
                -DTEST_MODE -DUNIT_TEST
ifeq ($(HAVE_PLIST_MEM_FREE),1)
    MOCK_CFLAGS += -DTP_PLIST_HAS_MEM_FREE=1
endif
ifeq ($(HAVE_PLIST_FROM_MEM4),1)
    MOCK_CFLAGS += -DTP_PLIST_HAS_FMT_ARG=1
endif
ifeq ($(HAVE_IRECV_NOTIFY),1)
    MOCK_CFLAGS += -DTP_IRECV_HAS_NOTIFY_FINISH=1
endif
MOCK_LDFLAGS  = $(shell pkg-config --libs $(MOCK_PKG_LIBS) 2>/dev/null)

test-mocks: $(MOCK_TARGET)
	./$(MOCK_TARGET)

$(MOCK_TARGET): $(MOCK_ALL_SRCS)
	$(CC) $(MOCK_CFLAGS) -o $@ $^ $(MOCK_LDFLAGS)

# Auto-generated header dependencies (from -MMD -MP in CFLAGS).
# -include so a missing .d (fresh checkout, before first build) is not an error.
-include $(SRCS:.c=.d)
