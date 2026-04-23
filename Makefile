CXX = g++
CXXFLAGS = -std=c++17 -Iinclude -Ilibs/imgui -Ilibs/imgui/backends -g -Wall
LDFLAGS = -lglfw -lGL -ldl -lpthread

IMGUI_DIR = libs/imgui
SOURCES = src/main.cpp src/process.cpp src/mem_scanner.cpp
SOURCES += $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_widgets.cpp $(IMGUI_DIR)/imgui_tables.cpp
SOURCES += $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp

OBJS = $(SOURCES:.cpp=.o)
EXE = lince

all: $(EXE)
	@echo Build complete for $(EXE)

$(EXE): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(EXE) $(OBJS)
