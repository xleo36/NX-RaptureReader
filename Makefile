CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -Iinclude -Isource -I/opt/homebrew/include -I/opt/homebrew/include/SDL2
LDFLAGS = -L/opt/homebrew/lib -lSDL2

BUILD_DIR = build
TARGET_PC = $(BUILD_DIR)/NX-RaptureReader-PC
SRCS = source/main.cpp

.PHONY: all pc run-pc clean

all: pc

pc: $(TARGET_PC)

$(TARGET_PC): $(SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

run-pc: pc
	./$(TARGET_PC)

clean:
	rm -rf $(BUILD_DIR)
