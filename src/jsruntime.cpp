#include "jsruntime.hpp"
#include "mem_scanner.hpp"
#include "process.hpp"
#include "quickjs.h"
#include <imgui.h>
#include <cstring>
#include <sstream>
#include <iostream>
#include <chrono>
#include <iomanip>

namespace laugh {

std::atomic<int> JavaScriptEngine::s_nextId(0);
JavaScriptEngine* JavaScriptEngine::s_current = nullptr;

JavaScriptEngine::JavaScriptEngine() : m_id(++s_nextId) {
}

JavaScriptEngine::~JavaScriptEngine() {
    if (m_ctx) {
        JS_FreeContext(m_ctx);
    }
    if (m_rt) {
        JS_FreeRuntime(m_rt);
    }
}

bool JavaScriptEngine::init() {
    m_rt = JS_NewRuntime();
    if (!m_rt) {
        m_lastError = "Failed to create QuickJS runtime";
        return false;
    }

    m_ctx = JS_NewContext(m_rt);
    if (!m_ctx) {
        m_lastError = "Failed to create QuickJS context";
        return false;
    }

    s_current = this;
    m_valid = true;

    setupBindings();
    return true;
}

void JavaScriptEngine::setMemoryScanner(void* scanner) {
    m_memoryScanner = scanner;
}

void JavaScriptEngine::setProcessList(void* processes) {
    m_processList = processes;
}

void JavaScriptEngine::addLog(ScriptLog::Level level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%H:%M:%S");
    
    m_logs.push_back({level, message, ss.str()});
    if (m_logs.size() > 1000) {
        m_logs.erase(m_logs.begin());
    }
}

static int32_t toInt32(JSContext* ctx, JSValueConst val) {
    int32_t v = 0;
    JS_ToInt32(ctx, &v, val);
    return v;
}

static int64_t toInt64(JSContext* ctx, JSValueConst val) {
    int64_t v = 0;
    JS_ToInt64(ctx, &v, val);
    return v;
}

static double toFloat64(JSContext* ctx, JSValueConst val) {
    double v = 0;
    JS_ToFloat64(ctx, &v, val);
    return v;
}

static bool toBool(JSContext* ctx, JSValueConst val) {
    return JS_ToBool(ctx, val) != 0;
}

void JavaScriptEngine::setupBindings() {
    if (!m_ctx) return;

    JSValue global = JS_GetGlobalObject(m_ctx);

    JSValue memoryObj = JS_NewObject(m_ctx);
    JS_SetPropertyStr(m_ctx, global, "memory", memoryObj);

    JS_DefinePropertyValueStr(m_ctx, memoryObj, "read",
        JS_NewCFunction(m_ctx, jsReadMemory, "read", 2), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, memoryObj, "write",
        JS_NewCFunction(m_ctx, jsWriteMemory, "write", 3), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, memoryObj, "scan",
        JS_NewCFunction(m_ctx, jsScanMemory, "scan", 0), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, memoryObj, "AOB",
        JS_NewCFunction(m_ctx, jsAOBScan, "AOB", 1), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(m_ctx, memoryObj, "getProcessInfo",
        JS_NewCFunction(m_ctx, jsGetProcessInfo, "getProcessInfo", 0), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);

    JSValue guiObj = JS_NewObject(m_ctx);
    JS_SetPropertyStr(m_ctx, global, "gui", guiObj);

    struct GUIBinding { const char* name; JSCFunction* func; int argc; } guiFuncs[] = {
        {"beginWindow", jsWindowBegin, 1}, {"endWindow", jsWindowEnd, 0},
        {"button", jsButton, 1}, {"text", jsText, 1},
        {"inputInt", jsInputInt, 2}, {"inputFloat", jsInputFloat, 2},
        {"checkbox", jsCheckbox, 2}, {"sliderFloat", jsSliderFloat, 4},
        {"separator", jsSeparator, 0}, {"sameLine", jsSameLine, 0},
        {"beginChild", jsBeginChild, 1}, {"endChild", jsEndChild, 0},
        {"progressBar", jsProgressBar, 1}, {"combo", jsCombo, 3},
        {"treeNode", jsTreeNode, 1}, {"treePop", jsTreePop, 0},
        {"getScreenSize", jsGetScreenSize, 0}, {"getFrameCount", jsGetFrameCount, 0},
        {"getDeltaTime", jsGetDeltaTime, 0}, {"isKeyPressed", jsIsKeyPressed, 1},
        {"isMouseClicked", jsIsMouseClicked, 1}, {"getMousePos", jsGetMousePos, 0},
        {"drawLine", jsDrawLine, 4}, {"drawRect", jsDrawRect, 4},
        {"drawCircle", jsDrawCircle, 3}, {"drawText", jsDrawText, 3},
    };

    for (const auto& f : guiFuncs) {
        JS_DefinePropertyValueStr(m_ctx, guiObj, f.name,
            JS_NewCFunction(m_ctx, f.func, f.name, f.argc), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    }

    JS_DefinePropertyValueStr(m_ctx, global, "log",
        JS_NewCFunction(m_ctx, jsLog, "log", 1), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);

    JS_FreeValue(m_ctx, global);

    const char* initCode = R"(
        var lastResult = [];
        var savedValues = new Map();
    )";
    JS_Eval(m_ctx, initCode, strlen(initCode), "<init>", JS_EVAL_TYPE_GLOBAL);
}

JSValue JavaScriptEngine::jsReadMemory(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || !s_current->m_memoryScanner) return JS_NULL;
    if (argc < 2) return JS_NULL;

    auto ms = static_cast<MemScanner*>(s_current->m_memoryScanner);
    uint64_t addr = toInt64(ctx, argv[0]);
    int type = toInt32(ctx, argv[1]);

    switch (type) {
        case 0: return JS_NewUint32(ctx, ms->readMemory<uint8_t>(addr));
        case 1: return JS_NewUint32(ctx, ms->readMemory<uint16_t>(addr));
        case 2: return JS_NewUint32(ctx, ms->readMemory<uint32_t>(addr));
        case 3: return JS_NewBigUint64(ctx, ms->readMemory<uint64_t>(addr));
        case 4: return JS_NewFloat64(ctx, ms->readMemory<float>(addr));
        case 5: return JS_NewFloat64(ctx, ms->readMemory<double>(addr));
        case 6: {
            std::string str = ms->readString(addr, 64);
            return JS_NewString(ctx, str.c_str());
        }
        default: return JS_NULL;
    }
}

JSValue JavaScriptEngine::jsWriteMemory(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || !s_current->m_memoryScanner) return JS_FALSE;
    if (argc < 3) return JS_FALSE;

    auto ms = static_cast<MemScanner*>(s_current->m_memoryScanner);
    uint64_t addr = toInt64(ctx, argv[0]);
    int type = toInt32(ctx, argv[2]);

    switch (type) {
        case 0: ms->writeMemory<uint8_t>(addr, (uint8_t)toInt32(ctx, argv[1])); break;
        case 1: ms->writeMemory<uint16_t>(addr, (uint16_t)toInt32(ctx, argv[1])); break;
        case 2: ms->writeMemory<uint32_t>(addr, (uint32_t)toInt32(ctx, argv[1])); break;
        case 3: ms->writeMemory<uint64_t>(addr, (uint64_t)toInt64(ctx, argv[1])); break;
        case 4: ms->writeMemory<float>(addr, (float)toFloat64(ctx, argv[1])); break;
        case 5: ms->writeMemory<double>(addr, (double)toFloat64(ctx, argv[1])); break;
        default: return JS_FALSE;
    }
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsScanMemory(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || !s_current->m_memoryScanner) {
        return JS_NewArray(ctx);
    }

    auto ms = static_cast<MemScanner*>(s_current->m_memoryScanner);
    auto results = ms->getResults();

    JSValue arr = JS_NewArray(ctx);
    uint32_t idx = 0;
    for (const auto& res : results) {
        JS_SetPropertyUint32(ctx, arr, idx++, JS_NewBigUint64(ctx, res.address));
    }
    return arr;
}

JSValue JavaScriptEngine::jsAOBScan(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current || !s_current->m_memoryScanner) return JS_NewArray(ctx);
    if (argc < 1) return JS_NewArray(ctx);

    const char* pattern = JS_ToCString(ctx, argv[0]);
    if (!pattern) return JS_NewArray(ctx);

    auto ms = static_cast<MemScanner*>(s_current->m_memoryScanner);
    auto results = ms->aobScan(pattern);
    JS_FreeCString(ctx, pattern);

    JSValue arr = JS_NewArray(ctx);
    for (uint32_t i = 0; i < results.size(); ++i) {
        JS_SetPropertyUint32(ctx, arr, i, JS_NewBigUint64(ctx, results[i].address));
    }
    return arr;
}

JSValue JavaScriptEngine::jsGetProcessInfo(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!s_current) return JS_NULL;

    JSValue obj = JS_NewObject(ctx);
    // TODO: Implement getting current attached process info
    JS_SetPropertyStr(ctx, obj, "pid", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, obj, "name", JS_NewString(ctx, "Unknown"));
    return obj;
}

JSValue JavaScriptEngine::jsWindowBegin(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_FALSE;
    const char* name = JS_ToCString(ctx, argv[0]);
    bool result = ImGui::Begin(name, nullptr, 0);
    JS_FreeCString(ctx, name);
    return result ? JS_TRUE : JS_FALSE;
}

JSValue JavaScriptEngine::jsWindowEnd(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    ImGui::End();
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsButton(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_FALSE;
    const char* label = JS_ToCString(ctx, argv[0]);
    bool result = ImGui::Button(label);
    JS_FreeCString(ctx, label);
    return result ? JS_TRUE : JS_FALSE;
}

JSValue JavaScriptEngine::jsText(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_TRUE;
    const char* text = JS_ToCString(ctx, argv[0]);
    ImGui::Text("%s", text ? text : "");
    if (text) JS_FreeCString(ctx, text);
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsInputInt(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_NewInt32(ctx, 0);
    const char* label = JS_ToCString(ctx, argv[0]);
    int value = toInt32(ctx, argv[1]);
    ImGui::InputInt(label, &value);
    JS_FreeCString(ctx, label);
    return JS_NewInt32(ctx, value);
}

JSValue JavaScriptEngine::jsInputFloat(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_NewFloat64(ctx, 0.0);
    const char* label = JS_ToCString(ctx, argv[0]);
    double value = toFloat64(ctx, argv[1]);
    float fval = (float)value;
    ImGui::InputFloat(label, &fval);
    JS_FreeCString(ctx, label);
    return JS_NewFloat64(ctx, fval);
}

JSValue JavaScriptEngine::jsCheckbox(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_FALSE;
    const char* label = JS_ToCString(ctx, argv[0]);
    bool value = toBool(ctx, argv[1]);
    ImGui::Checkbox(label, &value);
    JS_FreeCString(ctx, label);
    return value ? JS_TRUE : JS_FALSE;
}

JSValue JavaScriptEngine::jsSliderFloat(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 4) return JS_NewFloat64(ctx, 0.0);
    const char* label = JS_ToCString(ctx, argv[0]);
    float value = (float)toFloat64(ctx, argv[1]);
    float vmin = (float)toFloat64(ctx, argv[2]);
    float vmax = (float)toFloat64(ctx, argv[3]);
    ImGui::SliderFloat(label, &value, vmin, vmax);
    JS_FreeCString(ctx, label);
    return JS_NewFloat64(ctx, value);
}

JSValue JavaScriptEngine::jsSeparator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    ImGui::Separator();
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsSameLine(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    float offset = 0.0f;
    float spacing = -1.0f;
    if (argc > 0) offset = (float)toFloat64(ctx, argv[0]);
    if (argc > 1) spacing = (float)toFloat64(ctx, argv[1]);
    ImGui::SameLine(offset, spacing);
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsBeginChild(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    const char* id = "";
    if (argc > 0) id = JS_ToCString(ctx, argv[0]);
    ImVec2 size(0, 0);
    if (argc > 1) {
        double a = toFloat64(ctx, argv[1]);
        double b = argc > 2 ? toFloat64(ctx, argv[2]) : 0;
        size = ImVec2((float)a, (float)b);
    }
    bool result = ImGui::BeginChild(id, size, true);
    if (id[0]) JS_FreeCString(ctx, id);
    return result ? JS_TRUE : JS_FALSE;
}

JSValue JavaScriptEngine::jsEndChild(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    ImGui::EndChild();
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsProgressBar(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_TRUE;
    float fraction = (float)toFloat64(ctx, argv[0]);
    ImGui::ProgressBar(fraction);
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsCombo(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_NewInt32(ctx, 0);
    const char* label = JS_ToCString(ctx, argv[0]);
    int current = toInt32(ctx, argv[1]);
    // TODO: Implement combo with items array
    JS_FreeCString(ctx, label);
    return JS_NewInt32(ctx, current);
}

JSValue JavaScriptEngine::jsTreeNode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_FALSE;
    const char* label = JS_ToCString(ctx, argv[0]);
    bool result = ImGui::TreeNode(label);
    JS_FreeCString(ctx, label);
    return result ? JS_TRUE : JS_FALSE;
}

JSValue JavaScriptEngine::jsTreePop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    ImGui::TreePop();
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsGetScreenSize(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto& io = ImGui::GetIO();
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, io.DisplaySize.x));
    JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, io.DisplaySize.y));
    return obj;
}

JSValue JavaScriptEngine::jsGetFrameCount(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_NewInt32(ctx, ImGui::GetFrameCount());
}

JSValue JavaScriptEngine::jsGetDeltaTime(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_NewFloat64(ctx, ImGui::GetIO().DeltaTime);
}

JSValue JavaScriptEngine::jsIsKeyPressed(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_FALSE;
    int key = toInt32(ctx, argv[0]);
    bool repeat = (argc > 1) ? toBool(ctx, argv[1]) : true;
    return ImGui::IsKeyPressed((ImGuiKey)key, repeat) ? JS_TRUE : JS_FALSE;
}

JSValue JavaScriptEngine::jsIsMouseClicked(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    int button = (argc > 0) ? toInt32(ctx, argv[0]) : 0;
    bool repeat = (argc > 1) ? toBool(ctx, argv[1]) : false;
    return ImGui::IsMouseClicked(button, repeat) ? JS_TRUE : JS_FALSE;
}

JSValue JavaScriptEngine::jsGetMousePos(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto& io = ImGui::GetIO();
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, io.MousePos.x));
    JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, io.MousePos.y));
    return obj;
}

JSValue JavaScriptEngine::jsLog(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    std::ostringstream oss;
    for (int i = 0; i < argc; i++) {
        if (i > 0) oss << " ";
        const char* s = JS_ToCString(ctx, argv[i]);
        oss << (s ? s : "undefined");
        if (s) JS_FreeCString(ctx, s);
    }
    if (s_current) {
        s_current->addLog(ScriptLog::Info, oss.str());
    }
    return JS_UNDEFINED;
}

JSValue JavaScriptEngine::jsDrawLine(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 4) return JS_TRUE;
    ImVec2 a((float)toFloat64(ctx, argv[0]), (float)toFloat64(ctx, argv[1]));
    ImVec2 b((float)toFloat64(ctx, argv[2]), (float)toFloat64(ctx, argv[3]));
    ImGui::GetWindowDrawList()->AddLine(a, b, IM_COL32_WHITE, 1.0f);
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsDrawRect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 4) return JS_TRUE;
    ImVec2 a((float)toFloat64(ctx, argv[0]), (float)toFloat64(ctx, argv[1]));
    ImVec2 b((float)toFloat64(ctx, argv[2]), (float)toFloat64(ctx, argv[3]));
    ImGui::GetWindowDrawList()->AddRect(a, b, IM_COL32_WHITE);
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsDrawCircle(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_TRUE;
    ImVec2 center((float)toFloat64(ctx, argv[0]), (float)toFloat64(ctx, argv[1]));
    float radius = (float)toFloat64(ctx, argv[2]);
    ImGui::GetWindowDrawList()->AddCircle(center, radius, IM_COL32_WHITE, 32, 1.0f);
    return JS_TRUE;
}

JSValue JavaScriptEngine::jsDrawText(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_TRUE;
    const char* text = JS_ToCString(ctx, argv[0]);
    ImVec2 pos((float)toFloat64(ctx, argv[1]), (float)toFloat64(ctx, argv[2]));
    ImGui::GetWindowDrawList()->AddText(pos, IM_COL32_WHITE, text);
    if (text) JS_FreeCString(ctx, text);
    return JS_TRUE;
}

bool JavaScriptEngine::execute(const std::string& code) {
    if (!m_ctx || !m_valid) {
        m_lastError = "Runtime not initialized";
        return false;
    }

    s_current = this;

    JSValue val = JS_Eval(m_ctx, code.c_str(), code.size(), "<script>", JS_EVAL_TYPE_GLOBAL);

    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(m_ctx);
        const char* str = JS_ToCString(m_ctx, exc);
        m_lastError = str ? str : "Unknown error";
        addLog(ScriptLog::Error, m_lastError);
        if (str) JS_FreeCString(m_ctx, str);
        JS_FreeValue(m_ctx, exc);
        if (m_errorHandler) m_errorHandler(m_lastError);
        JS_FreeValue(m_ctx, val);
        return false;
    }

    JS_FreeValue(m_ctx, val);
    m_lastError.clear();
    return true;
}

void JavaScriptEngine::triggerUpdate() {
    if (!m_ctx || !m_valid) return;
    s_current = this;
    
    JSValue global = JS_GetGlobalObject(m_ctx);
    JSValue onUpdate = JS_GetPropertyStr(m_ctx, global, "onUpdate");
    if (JS_IsFunction(m_ctx, onUpdate)) {
        JSValue res = JS_Call(m_ctx, onUpdate, global, 0, nullptr);
        if (JS_IsException(res)) {
            JSValue exc = JS_GetException(m_ctx);
            const char* str = JS_ToCString(m_ctx, exc);
            addLog(ScriptLog::Error, std::string("Error in onUpdate: ") + (str ? str : "Unknown"));
            if (str) JS_FreeCString(m_ctx, str);
            JS_FreeValue(m_ctx, exc);
        }
        JS_FreeValue(m_ctx, res);
    }
    JS_FreeValue(m_ctx, onUpdate);
    JS_FreeValue(m_ctx, global);
}

void JavaScriptEngine::triggerGUI() {
    if (!m_ctx || !m_valid) return;
    s_current = this;
    
    JSValue global = JS_GetGlobalObject(m_ctx);
    JSValue onGUI = JS_GetPropertyStr(m_ctx, global, "onGUI");
    if (JS_IsFunction(m_ctx, onGUI)) {
        JSValue res = JS_Call(m_ctx, onGUI, global, 0, nullptr);
        if (JS_IsException(res)) {
            JSValue exc = JS_GetException(m_ctx);
            const char* str = JS_ToCString(m_ctx, exc);
            addLog(ScriptLog::Error, std::string("Error in onGUI: ") + (str ? str : "Unknown"));
            if (str) JS_FreeCString(m_ctx, str);
            JS_FreeValue(m_ctx, exc);
        }
        JS_FreeValue(m_ctx, res);
    }
    JS_FreeValue(m_ctx, onGUI);
    JS_FreeValue(m_ctx, global);
}

} // namespace laugh
