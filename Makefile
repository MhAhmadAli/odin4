# Odin4 Makefile

CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -O2
CXXFLAGS += -I./include
CXXFLAGS += -DODIN4_VERSION=\"1.2.1\" -DODIN4_VERSION_STRING=\"1.2.1-dc05e3ea\"

# Check for macOS
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS specific settings
    CXXFLAGS += -I/opt/homebrew/include -I/usr/local/include
    LDFLAGS = -L/opt/homebrew/lib -L/usr/local/lib
    # Try to find libusb
    LDFLAGS += $(shell pkg-config --libs libusb-1.0 2>/dev/null || echo "-lusb-1.0")
    CXXFLAGS += $(shell pkg-config --cflags libusb-1.0 2>/dev/null)
else
    # Linux
    LDFLAGS = $(shell pkg-config --libs libusb-1.0)
    CXXFLAGS += $(shell pkg-config --cflags libusb-1.0)
endif

# Libraries
LDFLAGS += -lz -llz4 -lpthread

# Optional crypto++ (if available)
CRYPTOPP := $(shell pkg-config --exists cryptopp 2>/dev/null && echo "yes")
ifeq ($(CRYPTOPP),yes)
    LDFLAGS += $(shell pkg-config --libs cryptopp)
    CXXFLAGS += $(shell pkg-config --cflags cryptopp) -DHAVE_CRYPTOPP
else
    # Try OpenSSL as fallback
    LDFLAGS += -lcrypto
endif

# Source files
SRCDIR = src
SOURCES = $(wildcard $(SRCDIR)/*.cpp)
OBJECTS = $(SOURCES:.cpp=.o)

# Target
TARGET = odin4

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(SRCDIR)/*.o $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

# Dependencies check
deps:
	@echo "Checking dependencies..."
	@pkg-config --exists libusb-1.0 && echo "  libusb-1.0: OK" || echo "  libusb-1.0: MISSING (brew install libusb)"
	@which lz4 >/dev/null 2>&1 && echo "  lz4: OK" || echo "  lz4: MISSING (brew install lz4)"
	@pkg-config --exists zlib && echo "  zlib: OK" || echo "  zlib: OK (built-in)"
	@pkg-config --exists cryptopp && echo "  cryptopp: OK" || echo "  cryptopp: MISSING (brew install cryptopp) - using OpenSSL instead"
