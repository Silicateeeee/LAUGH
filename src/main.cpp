#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <iomanip>
#include <fstream>
#include <sstream>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#define GL_SILENCE_DEPRECATION
#include "glfw3.h"

#include "process.hpp"
#include "mem_scanner.hpp"
#include "jsruntime.hpp"

bool processNameContains(const std::string& name, const std::string& filter);

MemScanner g_scanner;
std::vector<ProcessInfo> g_processes;
ProcessInfo g_selectedProcess = {-1, "None", ""};
bool g_showProcessSelector = false;
bool g_showSettings = false;

// Global JS window state
GLFWwindow* g_jsOutputWindow = nullptr;
ImGuiContext* g_jsOutputContext = nullptr;

std::unique_ptr<laugh::JavaScriptEngine> g_jsEngine;
bool g_showScriptPanel = false;
char g_scriptCodeBuffer[32768] = "";
char g_scriptPath[256] = "myscript.js";

const char* DEFAULT_SCRIPT = R"(
// Welcome to LAUGH JavaScript scripting!
// Access memory with: memory.read(address, type)
// memory.write(address, value, type)
// memory.scan() - returns all addresses
// Type: 0=byte, 1=2bytes, 2=4bytes, 3=8bytes, 4=float, 5=double, 6=string

function onUpdate() {
    // Your code here - called every frame
}

function onGUI() {
    // Your custom GUI code here
    gui.text("Script Panel loaded!");
    if (gui.button("Click Me")) {
        log("Button clicked!");
    }
}
)";

struct Settings {
    int alignment = 4;
    bool darkMode = true;
    bool scanRead = true;
    bool scanWrite = false;
    bool scanExec = false;
    bool excludeKernel = true;
    int maxResults = 10000;
    float uiScale = 1.0f;
    float rounding = 3.0f;
} g_settings;

struct SavedAddress {
    bool active;
    std::string description;
    uintptr_t address;
    ValueType type;
    std::string value;
};
std::vector<SavedAddress> g_savedAddresses;

enum class EditField { None, Description, Address, Type, Value };
struct EditState {
    int index = -1;
    EditField field = EditField::None;
    char buffer[256] = "";
    int session = 0;
} g_editState;

char g_searchValue[128] = "100";
int g_selectedType = 2;
const char* g_types[] = { "Byte", "2 Bytes", "4 Bytes", "8 Bytes", "Float", "Double", "String", "AOB" };

static void SetEditBuffer(const char* str) {
    memset(g_editState.buffer, 0, sizeof(g_editState.buffer));
    snprintf(g_editState.buffer, sizeof(g_editState.buffer), "%s", str);
    g_editState.session++;
}

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static void SyncContextSettings(GLFWwindow* targetWin = nullptr) {
    // Sync ImGui Style to current context
    if (g_settings.darkMode) ImGui::StyleColorsDark();
    else ImGui::StyleColorsLight();
    
    ImGui::GetIO().FontGlobalScale = g_settings.uiScale;
    ImGui::GetStyle().FrameRounding = g_settings.rounding;

    // Sync Process info to JS Engine
    if (g_jsEngine) {
        g_jsEngine->setAttachedProcess(g_selectedProcess.pid, g_selectedProcess.name);
    }

    // Sync Window Title if this is the secondary window
    if (targetWin && targetWin == g_jsOutputWindow) {
        char title[256];
        snprintf(title, sizeof(title), "LAUGH Script GUI - %s [%d]", g_selectedProcess.name.c_str(), g_selectedProcess.pid);
        glfwSetWindowTitle(targetWin, title);
    }
}

void DrawProcessSelector() {
    if (ImGui::BeginPopupModal("Select Process", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char filter[128] = "";
        ImGui::InputText("Filter", filter, IM_ARRAYSIZE(filter));

        if (ImGui::Button("Refresh")) {
            g_processes = getRunningProcesses();
        }

        ImGui::BeginChild("ProcessList", ImVec2(500, 300), true);
        for (const auto& proc : g_processes) {
            if (!processNameContains(proc.name, filter)) continue;

            char label[256];
            snprintf(label, sizeof(label), "[%d] %s", proc.pid, proc.name.c_str());
            if (ImGui::Selectable(label, g_selectedProcess.pid == proc.pid)) {
                if (g_scanner.attach(proc.pid)) {
                    g_selectedProcess = proc;
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndChild();

        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void DrawTopLeft() {
    ImGui::BeginChild("Results", ImVec2(ImGui::GetContentRegionAvail().x * 0.4f, ImGui::GetContentRegionAvail().y * 0.6f), true);

    auto results = g_scanner.getResults();
    ImGui::Text("Found: %zu", results.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Add All")) {
        for (const auto& res : results) {
            g_savedAddresses.push_back({false, "No description", res.address, (ValueType)g_selectedType, "???"});
        }
    }

    if (g_scanner.isScanning()) {
        ImGui::ProgressBar(g_scanner.getProgress(), ImVec2(-1, 0), "Scanning...");
    }

    if (ImGui::BeginTable("ResultsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < std::min(results.size(), (size_t)1000); ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            char addrStr[32];
            snprintf(addrStr, sizeof(addrStr), "%p", (void*)results[i].address);
            if (ImGui::Selectable(addrStr, false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(0)) {
                    g_savedAddresses.push_back({false, "No description", results[i].address, (ValueType)g_selectedType, "???"});
                }
            }

            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Add to Address List")) {
                    g_savedAddresses.push_back({false, "No description", results[i].address, (ValueType)g_selectedType, "???"});
                }
                ImGui::EndPopup();
            }

            ImGui::TableSetColumnIndex(1);
            if (g_selectedType == 6) {
                std::string val = g_scanner.readString(results[i].address, 16);
                ImGui::Text("%s", val.c_str());
            } else {
                uint32_t val = g_scanner.readMemory<uint32_t>(results[i].address);
                ImGui::Text("%u", val);
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void DrawSettingsWindow() {
    if (!g_showSettings) return;
    if (ImGui::Begin("Settings", &g_showSettings, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginTabBar("SettingsTabs")) {
            if (ImGui::BeginTabItem("General")) {
                if (ImGui::Checkbox("Dark Mode", &g_settings.darkMode)) {
                    if (g_settings.darkMode) ImGui::StyleColorsDark();
                    else ImGui::StyleColorsLight();
                }
                ImGui::SliderInt("Max Results", &g_settings.maxResults, 1000, 100000);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Scanner")) {
                ImGui::Text("Memory Filters");
                ImGui::Checkbox("Scan Readable", &g_settings.scanRead);
                ImGui::Checkbox("Scan Writable", &g_settings.scanWrite);
                ImGui::Checkbox("Scan Executable", &g_settings.scanExec);
                ImGui::Checkbox("Exclude Kernel Addresses", &g_settings.excludeKernel);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Visuals")) {
                if (ImGui::SliderFloat("UI Scale", &g_settings.uiScale, 0.5f, 2.0f)) {
                    ImGui::GetIO().FontGlobalScale = g_settings.uiScale;
                }
                if (ImGui::SliderFloat("Rounding", &g_settings.rounding, 0.0f, 12.0f)) {
                    ImGui::GetStyle().FrameRounding = g_settings.rounding;
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (ImGui::Button("Close")) g_showSettings = false;
        ImGui::End();
    }
}

void DrawTopRight() {
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::BeginChild("Controls", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.6f), true);

    ImGui::Text("Process: [%d] %s", g_selectedProcess.pid, g_selectedProcess.name.c_str());
    ImGui::BeginDisabled(g_scanner.isScanning());
    if (ImGui::Button("Select Process")) {
        g_processes = getRunningProcesses();
        g_showProcessSelector = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Settings")) {
        g_showSettings = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Scripts")) {
        g_showScriptPanel = true;
    }

    ImGui::Separator();

    ImGui::InputText("Value", g_searchValue, IM_ARRAYSIZE(g_searchValue));
    ImGui::Combo("Value Type", &g_selectedType, g_types, IM_ARRAYSIZE(g_types));

    if (ImGui::Button("First Scan", ImVec2(100, 0))) {
        g_scanner.firstScan((ValueType)g_selectedType, g_searchValue);
    }
    ImGui::SameLine();
    if (ImGui::Button("Next Scan", ImVec2(100, 0))) {
        g_scanner.nextScan((ValueType)g_selectedType, g_searchValue);
    }
    ImGui::SameLine();
    if (ImGui::Button("New Scan", ImVec2(100, 0))) {
        g_scanner.clearResults();
    }
    ImGui::EndDisabled();

    ImGui::EndChild();
    ImGui::EndGroup();
}

void DrawDocumentation() {
    ImGui::TextColored(ImVec4(1, 1, 0, 1), "LAUGH Scripting Documentation");
    ImGui::Separator();

    if (ImGui::BeginTabBar("DocTabs")) {
        if (ImGui::BeginTabItem("Memory")) {
            ImGui::Text("memory.read(address, type)");
            ImGui::BulletText("address: BigInt or Number");
            ImGui::BulletText("type: 0=Byte, 1=2Bytes, 2=4Bytes, 3=8Bytes, 4=Float, 5=Double, 6=String");
            ImGui::Separator();
            ImGui::Text("memory.write(address, value, type)");
            ImGui::BulletText("value: Number, BigInt, or String (for type 7)");
            ImGui::BulletText("type: 0-6 (Numeric/String), 7 (AOB Pattern)");
            ImGui::Separator();
            ImGui::Text("memory.scan()");
            ImGui::BulletText("Returns an array of BigInt addresses from the last scan results.");
            ImGui::Separator();
            ImGui::Text("memory.AOB(pattern)");
            ImGui::BulletText("pattern: String (e.g., \"00 FA AF ?? 00\")");
            ImGui::BulletText("Returns a Promise resolving to an array of BigInt addresses.");
            ImGui::Separator();
            ImGui::Text("memory.isScanning() - Returns true if a background scan is active.");
            ImGui::Text("memory.getProgress() - Returns 0.0 to 1.0.");
            ImGui::Text("memory.getResults() - Returns latest results array.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("GUI")) {
            ImGui::Text("gui.button(label) - Returns true if clicked");
            ImGui::Text("gui.text(text)");
            ImGui::Text("gui.checkbox(label, value) - Returns new value");
            ImGui::Text("gui.inputInt(label, value) - Returns new value");
            ImGui::Text("gui.inputFloat(label, value) - Returns new value");
            ImGui::Text("gui.sliderFloat(label, value, min, max) - Returns new value");
            ImGui::Text("gui.sameLine()");
            ImGui::Text("gui.separator()");
            ImGui::Text("gui.beginWindow(name) / gui.endWindow()");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Hooks")) {
            ImGui::Text("function onUpdate() { ... }");
            ImGui::BulletText("Called every frame. Useful for background memory manipulation.");
            ImGui::Separator();
            ImGui::Text("function onGUI() { ... }");
            ImGui::BulletText("Called during UI rendering. Use gui.* functions here.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void InitJSOutputWindow(const char* glsl_version) {
    if (g_jsOutputWindow) return;

    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    g_jsOutputWindow = glfwCreateWindow(640, 480, "LAUGH Script Output", NULL, NULL);
    if (!g_jsOutputWindow) return;

    // Save current
    GLFWwindow* main_win = glfwGetCurrentContext();
    ImGuiContext* main_ctx = ImGui::GetCurrentContext();

    glfwMakeContextCurrent(g_jsOutputWindow);
    g_jsOutputContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(g_jsOutputContext);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(g_jsOutputWindow, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Restore
    glfwMakeContextCurrent(main_win);
    ImGui::SetCurrentContext(main_ctx);
}

void DrawScriptPanel() {
    if (!g_showScriptPanel) return;

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Script Engine", &g_showScriptPanel)) {
        if (ImGui::BeginTabBar("ScriptPanelTabs")) {
            if (ImGui::BeginTabItem("Editor")) {
                // Toolbar
                if (ImGui::Button("Execute")) {
                    g_jsEngine->execute(g_scriptCodeBuffer);
                    if (!g_jsOutputWindow) {
                        InitJSOutputWindow("#version 130");
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Open GUI Window")) {
                    InitJSOutputWindow("#version 130");
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset Runtime")) {
                    g_jsEngine->init();
                    g_jsEngine->setMemoryScanner(&g_scanner);
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200);
                ImGui::InputText("##path", g_scriptPath, IM_ARRAYSIZE(g_scriptPath));
                ImGui::SameLine();
                if (ImGui::Button("Load")) {
                    std::ifstream t(g_scriptPath);
                    if (t.is_open()) {
                        std::stringstream buffer;
                        buffer << t.rdbuf();
                        std::string content = buffer.str();
                        strncpy(g_scriptCodeBuffer, content.c_str(), sizeof(g_scriptCodeBuffer)-1);
                        g_jsEngine->addLog(laugh::ScriptLog::Info, "Loaded script from " + std::string(g_scriptPath));
                    } else {
                        g_jsEngine->addLog(laugh::ScriptLog::Error, "Failed to open file: " + std::string(g_scriptPath));
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Save")) {
                    std::ofstream t(g_scriptPath);
                    if (t.is_open()) {
                        t << g_scriptCodeBuffer;
                        g_jsEngine->addLog(laugh::ScriptLog::Info, "Saved script to " + std::string(g_scriptPath));
                    } else {
                        g_jsEngine->addLog(laugh::ScriptLog::Error, "Failed to save file: " + std::string(g_scriptPath));
                    }
                }

                ImGui::Separator();

                // Editor and Log
                float availH = ImGui::GetContentRegionAvail().y;
                ImGui::InputTextMultiline("##code", g_scriptCodeBuffer, IM_ARRAYSIZE(g_scriptCodeBuffer),
                    ImVec2(-1, availH * 0.7f), ImGuiInputTextFlags_AllowTabInput);

                ImGui::Separator();
                ImGui::Text("Console:");
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) g_jsEngine->clearLogs();
                
                ImGui::BeginChild("ConsoleLog", ImVec2(0, 0), true);
                for (const auto& log : g_jsEngine->getLogs()) {
                    ImVec4 color(1, 1, 1, 1);
                    if (log.level == laugh::ScriptLog::Error) color = ImVec4(1, 0.4f, 0.4f, 1);
                    else if (log.level == laugh::ScriptLog::Warning) color = ImVec4(1, 1, 0.4f, 1);
                    
                    ImGui::TextDisabled("[%s]", log.timestamp.c_str());
                    ImGui::SameLine();
                    ImGui::TextColored(color, "%s", log.message.c_str());
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0f);
                ImGui::EndChild();

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Documentation")) {
                DrawDocumentation();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void DrawEditPopups() {
    if (g_editState.field == EditField::None) return;

    ImGui::OpenPopup("Edit Field");
    if (ImGui::BeginPopupModal("Edit Field", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {

        ImGui::Text("Editing %s",
                    g_editState.field == EditField::Description ? "Description" :
                    g_editState.field == EditField::Address ? "Address" :
                    g_editState.field == EditField::Value ? "Value" : "Type");

        if (g_editState.field == EditField::Type) {
            static int typeIdx = 0;
            ImGui::Combo("Type", &typeIdx, g_types, IM_ARRAYSIZE(g_types));
            if (ImGui::Button("OK")) {
                g_savedAddresses[g_editState.index].type = (ValueType)typeIdx;
                g_editState.field = EditField::None;
                ImGui::CloseCurrentPopup();
            }
        } else {
            char inputLabel[32];
            snprintf(inputLabel, sizeof(inputLabel), "New Value##%d", g_editState.session);
            ImGui::InputText(inputLabel, g_editState.buffer, IM_ARRAYSIZE(g_editState.buffer));

            if (ImGui::Button("OK", ImVec2(120, 0))) {
                auto& addr = g_savedAddresses[g_editState.index];
                try {
                    if (g_editState.field == EditField::Value) {
                        if (addr.type == ValueType::String) {
                            size_t len = strlen(g_editState.buffer);
                            for (size_t j = 0; j < len; j++) {
                                g_scanner.writeMemory<uint8_t>(addr.address + j, (uint8_t)g_editState.buffer[j]);
                            }
                            g_scanner.writeMemory<uint8_t>(addr.address + len, 0);
                        } else {
                            uint32_t val = (uint32_t)std::stoul(g_editState.buffer);
                            g_scanner.writeMemory<uint32_t>(addr.address, val);
                        }
                    } else if (g_editState.field == EditField::Description) {
                        addr.description = g_editState.buffer;
                    } else if (g_editState.field == EditField::Address) {
                        addr.address = std::stoull(g_editState.buffer, nullptr, 16);
                    }
                } catch (...) {
                }
                g_editState.field = EditField::None;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            g_editState.field = EditField::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void DrawBottom() {
    ImGui::BeginChild("AddressList", ImVec2(0, 0), true);
    if (ImGui::BeginTable("SavedAddresses", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Description");
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();

        int deleteIdx = -1;
        for (size_t i = 0; i < g_savedAddresses.size(); ++i) {
            auto& addr = g_savedAddresses[i];
            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("##active", &addr.active);

            auto CellItem = [&](const char* label, EditField field, const char* bufferInit, int colIdx) {
                ImGui::PushID(colIdx);
                if (ImGui::Selectable(label, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        g_editState.index = i;
                        g_editState.field = field;
                        SetEditBuffer(bufferInit);
                    }
                }
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Delete")) deleteIdx = i;
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            };

            ImGui::TableSetColumnIndex(1);
            CellItem(addr.description.c_str(), EditField::Description, addr.description.c_str(), 1);

            ImGui::TableSetColumnIndex(2);
            char addrStr[32];
            snprintf(addrStr, sizeof(addrStr), "%p", (void*)addr.address);
            CellItem(addrStr, EditField::Address, addrStr + 2, 2);

            ImGui::TableSetColumnIndex(3);
            CellItem(g_types[(int)addr.type], EditField::Type, "", 3);

            ImGui::TableSetColumnIndex(4);
            std::string valDisplay;
            if (addr.type == ValueType::String) valDisplay = g_scanner.readString(addr.address, 32);
            else {
                uint32_t currentVal = g_scanner.readMemory<uint32_t>(addr.address);
                valDisplay = std::to_string(currentVal);
            }
            CellItem(valDisplay.c_str(), EditField::Value, valDisplay.c_str(), 4);

            ImGui::PopID();
        }
        if (deleteIdx != -1) {
            g_savedAddresses.erase(g_savedAddresses.begin() + deleteIdx);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

int main(int, char**) {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return 1;

    g_jsEngine = std::make_unique<laugh::JavaScriptEngine>();
    if (!g_jsEngine->init()) {
        fprintf(stderr, "Failed to initialize JavaScript engine: %s\n", g_jsEngine->getLastError().c_str());
    } else {
        g_jsEngine->setMemoryScanner(&g_scanner);
        g_jsEngine->setProcessList(&g_processes);
        strncpy(g_scriptCodeBuffer, DEFAULT_SCRIPT, sizeof(g_scriptCodeBuffer)-1);
    }

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "LAUGH", NULL, NULL);
    if (window == NULL) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Sync main window context settings and update engine
        SyncContextSettings(window);
        if (g_jsEngine && g_jsEngine->isValid()) {
            g_jsEngine->triggerUpdate();
        }

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Main", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

        DrawTopLeft();
        DrawTopRight();
        DrawBottom();

        if (g_showProcessSelector) {
            ImGui::OpenPopup("Select Process");
            g_showProcessSelector = false;
        }
        DrawProcessSelector();
        DrawEditPopups();
        DrawSettingsWindow();
        DrawScriptPanel();
        
        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Handle JS Output Window
        if (g_jsOutputWindow) {
            if (glfwWindowShouldClose(g_jsOutputWindow)) {
                ImGuiContext* main_ctx = ImGui::GetCurrentContext();
                glfwMakeContextCurrent(g_jsOutputWindow);
                ImGui::SetCurrentContext(g_jsOutputContext);
                ImGui_ImplOpenGL3_Shutdown();
                ImGui_ImplGlfw_Shutdown();
                ImGui::DestroyContext(g_jsOutputContext);
                glfwDestroyWindow(g_jsOutputWindow);
                g_jsOutputWindow = nullptr;
                g_jsOutputContext = nullptr;
                glfwMakeContextCurrent(window);
                ImGui::SetCurrentContext(main_ctx);
            } else {
                ImGuiContext* main_ctx = ImGui::GetCurrentContext();
                glfwMakeContextCurrent(g_jsOutputWindow);
                ImGui::SetCurrentContext(g_jsOutputContext);

                // Sync style/settings and window title to secondary context
                SyncContextSettings(g_jsOutputWindow);

                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();

                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
                ImGui::Begin("JS Script GUI", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
                if (g_jsEngine && g_jsEngine->isValid()) {
                    g_jsEngine->triggerGUI();
                }
                ImGui::End();

                ImGui::Render();
                int out_w, out_h;
                glfwGetFramebufferSize(g_jsOutputWindow, &out_w, &out_h);
                glViewport(0, 0, out_w, out_h);
                glClearColor(0.1f, 0.1f, 0.1f, 1.00f);
                glClear(GL_COLOR_BUFFER_BIT);
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                glfwSwapBuffers(g_jsOutputWindow);

                // Restore main context
                glfwMakeContextCurrent(window);
                ImGui::SetCurrentContext(main_ctx);
            }
        }

        glfwSwapBuffers(window);
    }

    if (g_jsOutputWindow) {
        glfwMakeContextCurrent(g_jsOutputWindow);
        ImGui::SetCurrentContext(g_jsOutputContext);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(g_jsOutputContext);
        glfwDestroyWindow(g_jsOutputWindow);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
