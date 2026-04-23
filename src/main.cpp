#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <iomanip>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

#include "process.hpp"
#include "mem_scanner.hpp"

bool processNameContains(const std::string& name, const std::string& filter);

MemScanner g_scanner;
std::vector<ProcessInfo> g_processes;
ProcessInfo g_selectedProcess = {-1, "None", ""};
bool g_showProcessSelector = false;
bool g_showSettings = false;

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
const char* g_types[] = { "Byte", "2 Bytes", "4 Bytes", "8 Bytes", "Float", "Double", "String" };
static void SetEditBuffer(const char* str) {
    memset(g_editState.buffer, 0, sizeof(g_editState.buffer));
    snprintf(g_editState.buffer, sizeof(g_editState.buffer), "%s", str);
    g_editState.session++;
}

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
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

        if (ImGui::IsWindowFocused() && ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_A)) {
            for (const auto& res : results) {
                g_savedAddresses.push_back({false, "No description", res.address, (ValueType)g_selectedType, "???"});
            }
        }

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
                            printf("Successfully wrote string \"%s\" (%zu bytes + null) to address %p.\n",
                                   g_editState.buffer, len, (void*)addr.address);
                        } else {
                            uint32_t val = (uint32_t)std::stoul(g_editState.buffer);
                            g_scanner.writeMemory<uint32_t>(addr.address, val);
                            printf("Successfully wrote uint32_t value %u to address %p.\n", val, (void*)addr.address);
                        }
                    } else if (g_editState.field == EditField::Description) {
                        addr.description = g_editState.buffer;
                        printf("Successfully updated description for address %p.\n", (void*)addr.address);
                    } else if (g_editState.field == EditField::Address) {
                        addr.address = std::stoull(g_editState.buffer, nullptr, 16);
                        printf("Successfully updated address to %p.\n", (void*)addr.address);
                    }
                } catch (...) {
                    fprintf(stderr, "Error updating field %d. Please check input format and permissions.\n", (int)g_editState.field);
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
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
            ImGui::SetWindowFontScale(0.8f);
            ImGui::Checkbox("##active", &addr.active);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleVar();

            auto CellItem = [&](const char* label, EditField field, const char* bufferInit, int colIdx) {
                ImGui::PushID(colIdx);
                if (ImGui::Selectable(label, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        g_editState.index = i;
                        g_editState.field = field;
                        SetEditBuffer(bufferInit);
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Double-click to edit");

                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Delete")) deleteIdx = i;
                    if (ImGui::MenuItem("Change Description")) {
                        g_editState.index = i;
                        g_editState.field = EditField::Description;
                        SetEditBuffer(addr.description.c_str());
                    }
                    if (ImGui::MenuItem("Change Address")) {
                        g_editState.index = i;
                        g_editState.field = EditField::Address;
                        char tmp[32];
                        snprintf(tmp, sizeof(tmp), "%lx", addr.address);
                        SetEditBuffer(tmp);
                    }
                    if (ImGui::MenuItem("Change Type")) {
                        g_editState.index = i;
                        g_editState.field = EditField::Type;
                    }
                    if (ImGui::MenuItem("Change Value")) {
                        g_editState.index = i;
                        g_editState.field = EditField::Value;
                        char tmp[32];
                        uint32_t currentVal = g_scanner.readMemory<uint32_t>(addr.address);
                        snprintf(tmp, sizeof(tmp), "%u", currentVal);
                        SetEditBuffer(tmp);
                    }
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

            if (addr.active) {
                uint32_t freezeVal = (uint32_t)std::stoul(g_searchValue);
                g_scanner.writeMemory<uint32_t>(addr.address, freezeVal);
            }
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

        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
