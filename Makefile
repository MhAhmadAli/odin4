# Odin4 Makefile

CXX ?= clang++
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic
CXXFLAGS += -I./include
CXXFLAGS += -DODIN4_VERSION=\"1.2.1\" -DODIN4_VERSION_STRING=\"1.2.1-dc05e3ea\"

PREFIX ?= /usr/local
DESTDIR ?=

PKG_CONFIG ?= pkg-config

# Check for macOS
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS specific settings
    CXXFLAGS += -I/opt/homebrew/include -I/usr/local/include
    LDFLAGS = -L/opt/homebrew/lib -L/usr/local/lib
else
    LDFLAGS =
endif

# libusb is required
LIBUSB_LIBS := $(shell $(PKG_CONFIG) --libs libusb-1.0 2>/dev/null)
ifeq ($(strip $(LIBUSB_LIBS)),)
    # pkg-config knows nothing about it; fall back to the conventional name and
    # let the linker report the problem if it really is missing.
    LIBUSB_LIBS := -lusb-1.0
endif
CXXFLAGS += $(shell $(PKG_CONFIG) --cflags libusb-1.0 2>/dev/null)
LDFLAGS += $(LIBUSB_LIBS)

# Optional lz4. Only advertise HAVE_LZ4 when the headers are really there;
# linking -llz4 unconditionally broke the build on machines without it.
LZ4_LIBS := $(shell $(PKG_CONFIG) --libs liblz4 2>/dev/null)
ifneq ($(strip $(LZ4_LIBS)),)
    CXXFLAGS += $(shell $(PKG_CONFIG) --cflags liblz4 2>/dev/null) -DHAVE_LZ4
    LDFLAGS += $(LZ4_LIBS)
endif

# Optional crypto++ (if available)
CRYPTOPP := $(shell $(PKG_CONFIG) --exists cryptopp 2>/dev/null && echo "yes")
ifeq ($(CRYPTOPP),yes)
    LDFLAGS += $(shell $(PKG_CONFIG) --libs cryptopp)
    CXXFLAGS += $(shell $(PKG_CONFIG) --cflags cryptopp) -DHAVE_CRYPTOPP
else
    # Try OpenSSL as fallback
    LDFLAGS += -lcrypto
endif

LDFLAGS += -lz -lpthread

# Source files
SRCDIR = src
BUILDDIR = build
SOURCES = $(wildcard $(SRCDIR)/*.cpp)
OBJECTS = $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(SOURCES))
DEPS = $(OBJECTS:.o=.d)

# Target
TARGET = odin4

.PHONY: all clean install uninstall deps

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

# -MMD -MP records each object's header dependencies, so editing a header
# rebuilds what uses it instead of nothing at all.
$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

-include $(DEPS)

clean:
	rm -rf $(BUILDDIR) $(TARGET)
	rm -f $(SRCDIR)/*.o $(SRCDIR)/*.d

DOCDIR = $(PREFIX)/share/doc/$(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/
	# The MIT/BSD/Apache components require their notices to travel with any
	# distribution, so install them next to the binary.
	install -d $(DESTDIR)$(DOCDIR)
	install -m 644 LICENSE THIRD-PARTY-NOTICES.md $(DESTDIR)$(DOCDIR)/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	rm -f $(DESTDIR)$(DOCDIR)/LICENSE $(DESTDIR)$(DOCDIR)/THIRD-PARTY-NOTICES.md
	-rmdir $(DESTDIR)$(DOCDIR) 2>/dev/null || true

# Dependencies check
deps:
	@echo "Checking dependencies..."
	@$(PKG_CONFIG) --exists libusb-1.0 && echo "  libusb-1.0: OK" || echo "  libusb-1.0: MISSING (brew install libusb / apt install libusb-1.0-0-dev)"
	@$(PKG_CONFIG) --exists liblz4 && echo "  liblz4: OK" || echo "  liblz4: MISSING (optional)"
	@$(PKG_CONFIG) --exists zlib && echo "  zlib: OK" || echo "  zlib: OK (built-in)"
	@$(PKG_CONFIG) --exists cryptopp && echo "  cryptopp: OK" || echo "  cryptopp: MISSING (brew install cryptopp) - using OpenSSL instead"
