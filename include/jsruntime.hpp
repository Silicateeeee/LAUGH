#ifndef JSRUNTIME_HPP
#define JSRUNTIME_HPP

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>

#include "quickjs.h"

namespace laugh {

struct ScriptLog {
    enum Level { Info, Error, Warning };
    Level level;
    std::string message;
    std::string timestamp;
};

class JavaScriptEngine {
public:
    JavaScriptEngine();
    ~JavaScriptEngine();

    bool init();
    bool execute(const std::string& code);
    std::string getLastError() const { return m_lastError; }
    bool isValid() const { return m_valid; }

    void setMemoryScanner(void* scanner);
    void setProcessList(void* processes);
    void setAttachedProcess(int pid, const std::string& name);

    void triggerUpdate();
    void triggerGUI();

    using UpdateCallback = std::function<void()>;
    void setOnUpdate(UpdateCallback cb) { m_onUpdate = cb; }

    JSContext* getContext() { return m_ctx; }

    void setErrorHandler(std::function<void(const std::string&)> cb) { m_errorHandler = cb; }
    
    const std::vector<ScriptLog>& getLogs() const { return m_logs; }
    void clearLogs() { m_logs.clear(); }
    void addLog(ScriptLog::Level level, const std::string& message);

    struct PendingPromise {
        JSValue resolve;
        JSValue reject;
    };

private:
    std::string m_lastError;
    std::vector<ScriptLog> m_logs;
    std::vector<PendingPromise> m_pendingPromises;
    bool m_wasScanning = false;

    UpdateCallback m_onUpdate;
    std::function<void(const std::string&)> m_errorHandler;

    ::JSRuntime* m_rt = nullptr;
    ::JSContext* m_ctx = nullptr;

    void* m_memoryScanner = nullptr;
    void* m_processList = nullptr;
    int m_attachedPid = -1;
    std::string m_attachedName = "None";

    bool m_valid = false;
    static std::atomic<int> s_nextId;
    int m_id;

    void setupBindings();
    static JavaScriptEngine* s_current;

    static JSValue jsReadMemory(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsWriteMemory(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsScanMemory(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsAOBScan(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsIsScanning(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsGetProgress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsGetResults(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsGetProcessInfo(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    static JSValue jsWindowBegin(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsWindowEnd(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsButton(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsText(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsInputInt(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsInputFloat(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsCheckbox(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsSliderFloat(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsSeparator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsSameLine(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsBeginChild(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsEndChild(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsProgressBar(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsCombo(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsTreeNode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsTreePop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    static JSValue jsGetScreenSize(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsGetFrameCount(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsGetDeltaTime(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsIsKeyPressed(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsIsMouseClicked(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsGetMousePos(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    static JSValue jsLog(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    static JSValue jsDrawLine(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsDrawRect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsDrawCircle(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue jsDrawText(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
};

} // namespace laugh

#endif
