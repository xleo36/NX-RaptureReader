CXX = clang++
CXXFLAGS = -std=c++17 -Wall -I.
TARGET = main

SRCS = $(wildcard *.cpp)
DEPS = $(wildcard *.hpp)

all: $(TARGET)
	./$(TARGET)

$(TARGET): $(SRCS) $(DEPS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
