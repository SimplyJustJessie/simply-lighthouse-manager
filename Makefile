CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g
LDFLAGS = -pthread -ldl

OPENVR_DIR = ../lib/openvr
SRC_DIR = src
BUILD_DIR = build

OPENVR_INCLUDE = $(OPENVR_DIR)/headers
OPENVR_LIB_DIR = $(OPENVR_DIR)/lib/linux64
COMMON_INCLUDE = $(SRC_DIR)/core $(SRC_DIR)/ui

DBUS_CFLAGS = $(shell pkg-config --cflags dbus-1)
INCLUDES = -I"$(OPENVR_INCLUDE)" -Isrc/core -Isrc/ui -Isrc/gui -I"$(IMGUI_DIR)" -I"$(IMGUI_DIR)/backends" $(DBUS_CFLAGS)

LIBS = -L"$(OPENVR_LIB_DIR)" -lopenvr_api -lbluetooth -ldbus-1 -lGL -lX11
GUI_LIBS = -L"$(OPENVR_LIB_DIR)" -lopenvr_api -lbluetooth -ldbus-1 -lGL -lX11 -lglfw

IMGUI_DIR = lib/imgui
IMGUI_SOURCES = $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_demo.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp
IMGUI_SOURCES += $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
IMGUI_OBJECTS = $(IMGUI_SOURCES:%.cpp=$(BUILD_DIR)/%.o)

.PHONY: all clean directories gui

CORE_SOURCES = $(SRC_DIR)/core/BaseStationDetector.cpp \
               $(SRC_DIR)/core/BaseStationController.cpp \
               $(SRC_DIR)/core/SteamVRMonitor.cpp \
               $(SRC_DIR)/core/OpenVRBaseStationDetector.cpp

UI_SOURCES = $(SRC_DIR)/ui/main.cpp
GUI_SOURCES = $(SRC_DIR)/gui/main.cpp

SOURCES = $(CORE_SOURCES) $(UI_SOURCES)
GUI_SOURCES_FULL = $(CORE_SOURCES) $(GUI_SOURCES)

OBJECTS = $(SOURCES:%.cpp=$(BUILD_DIR)/%.o)
GUI_OBJECTS = $(GUI_SOURCES_FULL:%.cpp=$(BUILD_DIR)/%.o)

TARGET = $(BUILD_DIR)/bin/lighthouse-manager
GUI_TARGET = $(BUILD_DIR)/bin/lighthouse-manager-gui

.PHONY: all clean directories

all: directories $(TARGET)

gui: directories $(GUI_TARGET)

directories:
	@mkdir -p $(BUILD_DIR)/bin
	@mkdir -p $(BUILD_DIR)/$(SRC_DIR)/core
	@mkdir -p $(BUILD_DIR)/$(SRC_DIR)/ui
	@mkdir -p $(BUILD_DIR)/$(SRC_DIR)/gui
	@mkdir -p $(BUILD_DIR)/$(IMGUI_DIR)/backends

$(BUILD_DIR)/%.o: %.cpp
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DLIGHTHOUSE_MANAGER_VERSION=\"1.0.0\" -c "$<" -o "$@"

$(TARGET): $(OBJECTS)
	@echo "Linking $@..."
	@mkdir -p $(dir $@)
	@if [ ! -f "$(OPENVR_LIB_DIR)/libopenvr_api.so" ]; then \
		echo "ERROR: OpenVR library not found at $(OPENVR_LIB_DIR)/libopenvr_api.so"; \
		exit 1; \
	fi
	@OPENVR_LIB_ABS=$$(cd "$(OPENVR_LIB_DIR)" && pwd); \
	$(CXX) $(OBJECTS) -o "$@" $(LDFLAGS) -L"$$OPENVR_LIB_ABS" -Wl,-rpath,"$$OPENVR_LIB_ABS" -Wl,-rpath,'$$ORIGIN/../../../lib/openvr/lib/linux64' -lopenvr_api -lbluetooth -ldbus-1 -lGL -lX11

$(GUI_TARGET): $(GUI_OBJECTS) $(IMGUI_OBJECTS)
	@echo "Linking $@..."
	@mkdir -p $(dir $@)
	@if [ ! -f "$(OPENVR_LIB_DIR)/libopenvr_api.so" ]; then \
		echo "ERROR: OpenVR library not found at $(OPENVR_LIB_DIR)/libopenvr_api.so"; \
		exit 1; \
	fi
	@OPENVR_LIB_ABS=$$(cd "$(OPENVR_LIB_DIR)" && pwd); \
	$(CXX) $(GUI_OBJECTS) $(IMGUI_OBJECTS) -o "$@" $(LDFLAGS) -L"$$OPENVR_LIB_ABS" -Wl,-rpath,"$$OPENVR_LIB_ABS" -Wl,-rpath,'$$ORIGIN/../../../lib/openvr/lib/linux64' -lopenvr_api -lbluetooth -ldbus-1 -lGL -lX11 -lglfw

$(BUILD_DIR)/$(IMGUI_DIR)/%.o: $(IMGUI_DIR)/%.cpp
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I"$(IMGUI_DIR)" -I"$(IMGUI_DIR)/backends" -c "$<" -o "$@"

clean:
	rm -rf $(BUILD_DIR)

install-deps:
	@echo "Checking dependencies..."
	@pkg-config --exists bluez || (echo "Error: bluez not found. Install the development package for your distribution:" && echo "  Ubuntu/Debian: sudo apt install libbluetooth-dev" && echo "  Fedora/RHEL: sudo dnf install bluez-libs-devel" && echo "  Arch Linux: sudo pacman -S bluez-libs" && exit 1)
	@test -f $(OPENVR_INCLUDE)/openvr.h || (echo "Error: OpenVR headers not found at $(OPENVR_INCLUDE)" && exit 1)
	@echo "All dependencies found!"

help:
	@echo "Lighthouse Manager - Linux Edition Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build the project (default)"
	@echo "  clean        - Remove build artifacts"
	@echo "  install-deps - Check if all dependencies are available"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Dependencies:"
	@echo "  - OpenVR headers: $(OPENVR_INCLUDE)"
	@echo "  - BlueZ library: libbluetooth-dev"

