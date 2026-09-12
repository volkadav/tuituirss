# SPDX-License-Identifier: MIT
# tuituirss — a terminal client for Tiny Tiny RSS
#
# Build:      make
# Test:       make test
# Sanitizers: make asan
# Analyzer:   make analyze
# Fuzz:       make fuzz          (requires clang)
# Clean:      make clean
#
# Dependencies (development headers): libcurl, jansson, ncursesw, libcrypto.
# They are discovered with pkg-config; override PKG_CONFIG if needed.

CC      ?= cc
PKG_CONFIG ?= pkg-config

PKGS    := libcurl jansson ncursesw libcrypto

CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
CFLAGS  += $(shell $(PKG_CONFIG) --cflags $(PKGS))
CPPFLAGS += -Isrc

LDLIBS  += $(shell $(PKG_CONFIG) --libs $(PKGS))

# Hardening (override HARDEN_CFLAGS/HARDEN_LDFLAGS to disable).
HARDEN_CFLAGS  ?= -D_FORTIFY_SOURCE=2 -fstack-protector-strong \
                  -fstack-clash-protection -fcf-protection \
                  -Wformat=2 -Wformat-security -Werror=format-security
HARDEN_LDFLAGS ?= -Wl,-z,relro,-z,now -fPIE -pie
CFLAGS  += $(HARDEN_CFLAGS)
LDFLAGS += $(HARDEN_LDFLAGS)

# `make SANITIZE=1` builds with AddressSanitizer + UBSan (see the `asan` target).
ifeq ($(SANITIZE),1)
CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer -O1
LDFLAGS += -fsanitize=address,undefined
endif

# `make ANALYZE=1` builds with GCC's static analyzer (see the `analyze` target).
ifeq ($(ANALYZE),1)
CFLAGS += -fanalyzer
endif

SRC_DIRS := src src/util src/config src/api src/model src/ui
SRC      := $(wildcard $(addsuffix /*.c,$(SRC_DIRS)))

LIB_SRC  := $(filter-out src/main.c,$(SRC))
LIB_OBJ  := $(LIB_SRC:.c=.o)
MAIN_OBJ := src/main.o
OBJ      := $(LIB_OBJ) $(MAIN_OBJ)

BIN      := tuituirss
MANPAGE  := docs/tuituirss.1

# Installation paths (override PREFIX/DESTDIR as usual).
PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin
MANDIR   ?= $(PREFIX)/share/man/man1

# Packaging (nfpm). `make pkg` builds .deb and .rpm; `make deb`/`make rpm`
# build one. The version is read from src/main.c, the architecture from the
# host (or the Debian architecture when dpkg is available).
NFPM         ?= nfpm
PKG_VERSION  := $(shell sed -n 's/.*#define TUIIRSS_VERSION "\([^"]*\)".*/\1/p' src/main.c)
PKG_ARCH_DEB := $(shell dpkg --print-architecture 2>/dev/null || (uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/'))
PKG_ARCH_RPM := $(shell uname -m)

TEST_SRC := $(wildcard tests/unit/*.c)
TEST_BIN := tests/unit/run

INTEG_SRC := tests/integration/integration.c
INTEG_BIN := tests/integration/run

FUZZ_CC   ?= clang
FUZZ_BIN  := tests/fuzz/fuzz
FUZZ_SRC  := tests/fuzz/fuzz_all.c
FUZZ_TIME ?= 30

.PHONY: all clean test integration asan analyze fuzz man install deb rpm pkg

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

man:
	man ./$(MANPAGE)

install: $(BIN)
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(MANDIR)
	install -m 0755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	install -m 0644 $(MANPAGE) $(DESTDIR)$(MANDIR)/tuituirss.1

nfpm-deb.yaml: nfpm.yaml.in
	@echo "Generating nfpm config (deb) for $(PKG_VERSION)..."
	sed -e 's/@PKG_VERSION@/$(PKG_VERSION)/g' \
	    -e 's/@PKG_ARCH@/$(PKG_ARCH_DEB)/g' \
	    nfpm.yaml.in > $@

nfpm-rpm.yaml: nfpm.yaml.in
	@echo "Generating nfpm config (rpm) for $(PKG_VERSION)..."
	sed -e 's/@PKG_VERSION@/$(PKG_VERSION)/g' \
	    -e 's/@PKG_ARCH@/$(PKG_ARCH_RPM)/g' \
	    nfpm.yaml.in > $@

deb: $(BIN) nfpm-deb.yaml
	@command -v $(NFPM) >/dev/null 2>&1 || { echo "Error: nfpm is required to build .deb packages."; echo "Install: go install github.com/goreleaser/nfpm/v2/cmd/nfpm@latest"; exit 1; }
	$(NFPM) package --packager deb -f nfpm-deb.yaml

rpm: $(BIN) nfpm-rpm.yaml
	@command -v $(NFPM) >/dev/null 2>&1 || { echo "Error: nfpm is required to build .rpm packages."; echo "Install: go install github.com/goreleaser/nfpm/v2/cmd/nfpm@latest"; exit 1; }
	$(NFPM) package --packager rpm -f nfpm-rpm.yaml

pkg: deb rpm

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(TEST_BIN): $(TEST_SRC) $(LIB_SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) -Itests/unit -o $@ $(TEST_SRC) $(LIB_SRC) $(LDLIBS)

test: $(TEST_BIN)
	./$(TEST_BIN)

$(INTEG_BIN): $(INTEG_SRC) $(LIB_SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) -o $@ $(INTEG_SRC) $(LIB_SRC) $(LDLIBS)

integration: $(INTEG_BIN)
	./$(INTEG_BIN)

# Run the unit tests under AddressSanitizer + UndefinedBehaviorSanitizer.
asan:
	$(MAKE) clean
	$(MAKE) SANITIZE=1 test

# Static analysis with GCC's -fanalyzer.
analyze:
	$(MAKE) clean
	$(MAKE) ANALYZE=1 all

$(FUZZ_BIN): $(FUZZ_SRC) $(LIB_SRC)
	$(FUZZ_CC) -std=c11 -g -O1 -fsanitize=fuzzer,address,undefined \
	    $(shell $(PKG_CONFIG) --cflags $(PKGS)) -Isrc -o $@ \
	    $(FUZZ_SRC) $(LIB_SRC) $(shell $(PKG_CONFIG) --libs $(PKGS))

fuzz: $(FUZZ_BIN)
	./$(FUZZ_BIN) -max_total_time=$(FUZZ_TIME)

clean:
	rm -f $(OBJ) $(BIN) $(TEST_BIN) $(INTEG_BIN) $(FUZZ_BIN) \
	    *.deb *.rpm nfpm-deb.yaml nfpm-rpm.yaml

-include $(OBJ:.o=.d)
