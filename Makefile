# Thin compatibility wrapper - the canonical build is CMake.
# Use: ./scripts/build.sh  or  cmake -B build && cmake --build build
BUILD_DIR = build
CMAKE_ARGS ?=

.PHONY: all gui clean

all:
	cmake -B $(BUILD_DIR) $(CMAKE_ARGS)
	cmake --build $(BUILD_DIR) --parallel

gui: all

clean:
	rm -rf $(BUILD_DIR)
