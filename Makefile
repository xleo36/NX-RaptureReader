CXX = clang++
PKG_CFLAGS := $(shell pkg-config --cflags sdl2 freetype2 zlib libwebp)
PKG_LIBS := $(shell pkg-config --libs sdl2 freetype2 zlib libwebp)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
HOST_FRAMEWORKS = -Wl,-framework,CoreGraphics -Wl,-framework,CoreFoundation
else
HOST_FRAMEWORKS =
endif

CXXFLAGS = -std=c++17 -Wall -Wextra -Iinclude -Isource -isystem third_party $(PKG_CFLAGS)
LDFLAGS = $(PKG_LIBS) $(HOST_FRAMEWORKS)

BUILD_DIR = build
BUILD_SWITCH_DIR = $(BUILD_DIR)/switch
TARGET_PC = $(BUILD_DIR)/NX-RaptureReader-PC
TARGET_TEST = $(BUILD_DIR)/NX-RaptureReader-Tests
TARGET_NRO = $(BUILD_DIR)/NX-RaptureReader.nro

SWITCH_IMAGE ?= nx-rapturereader-switch
DOCKER_PLATFORM ?= linux/amd64

CORE_SRCS = source/app_controller.cpp source/cbz.cpp source/epub.cpp \
            source/library.cpp source/pdf.cpp source/progress.cpp \
            source/reader.cpp source/settings.cpp source/sleep_guard.cpp \
            source/text_renderer.cpp source/touch.cpp source/ui.cpp \
            source/zip_reader.cpp
SRCS = source/main.cpp $(CORE_SRCS)
TEST_SRCS = tests/test_main.cpp $(CORE_SRCS)

.PHONY: all pc run-pc test switch-image switch nxlink clean

all: pc

pc: $(TARGET_PC)

$(TARGET_PC): $(SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

run-pc: pc
	./$(TARGET_PC)

$(TARGET_TEST): $(TEST_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test: $(TARGET_TEST)
	./$(TARGET_TEST)

switch-image:
	docker build --platform $(DOCKER_PLATFORM) -t $(SWITCH_IMAGE) -f Dockerfile.switch .

switch:
	@docker image inspect $(SWITCH_IMAGE) >/dev/null 2>&1 || $(MAKE) switch-image
	docker run --rm --platform $(DOCKER_PLATFORM) -v "$(CURDIR):/work" -w /work $(SWITCH_IMAGE) \
		bash -c 'cmake -S . -B $(BUILD_SWITCH_DIR) -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake -DCMAKE_BUILD_TYPE=Release && cmake --build $(BUILD_SWITCH_DIR) -j"$$(nproc)" && cp $(BUILD_SWITCH_DIR)/NX-RaptureReader.nro $(TARGET_NRO)'
	@ls -lh $(TARGET_NRO)

nxlink: switch
	@test -n "$(SWITCH_IP)" || (echo "Usage: make nxlink SWITCH_IP=192.168.x.x"; exit 1)
	docker run --rm --platform $(DOCKER_PLATFORM) -v "$(CURDIR):/work" -w /work $(SWITCH_IMAGE) \
		/opt/devkitpro/tools/bin/nxlink -a $(SWITCH_IP) $(TARGET_NRO)

clean:
	rm -rf $(BUILD_DIR)
