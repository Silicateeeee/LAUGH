CXX = g++
CXXFLAGS = -std=c++17 -Iinclude -Ilibs/imgui -Ilibs/imgui/backends -Ilibs/imgui/backends/GLFW -Ilibs/quickjs/include -Ilibs/glfw/include -g -Wall
LDFLAGS = -Llibs/quickjs -Llibs/glfw -lGL -ldl -lpthread -lm -lX11 -lquickjs -lglfw3

IMGUI_DIR = libs/imgui
SOURCES = src/main.cpp src/process.cpp src/mem_scanner.cpp src/jsruntime.cpp \
          $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_widgets.cpp $(IMGUI_DIR)/imgui_tables.cpp \
          $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp

OBJS = $(SOURCES:.cpp=.o)
EXE = LAUGH

all: $(EXE)
	@echo Build complete for $(EXE)

$(EXE): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(EXE) $(OBJS)
