#pragma once
#include <vector>
#include <string>
#include <sys/types.h>
#include <cstdint>

enum class ValueType {
    Byte,
    TwoBytes,
    FourBytes,
    EightBytes,
    Float,
    Double,
    String,
    AOB
};

struct MemoryRegion {
    uintptr_t start;
    uintptr_t end;
    std::string permissions;
    std::string pathname;
};

struct ScanResult {
    uintptr_t address;
};

#include <atomic>
#include <mutex>
#include <thread>
#include <future>

class MemScanner {
public:
    MemScanner();
    ~MemScanner();
    bool attach(pid_t pid);
    void detach();
    
    bool isAttached() const { return m_pid != -1; }
    pid_t getPid() const { return m_pid; }
    bool isScanning() const { return m_isScanning; }
    float getProgress() const { return m_progress; }

    std::vector<MemoryRegion> getRegions();
    
    template<typename T>
    T readMemory(uintptr_t address);
    
    template<typename T>
    bool writeMemory(uintptr_t address, T value);

    std::string readString(uintptr_t address, size_t maxLength = 256);
    bool writeString(uintptr_t address, const std::string& value);

    void firstScan(ValueType type, const std::string& valueStr);
    void nextScan(ValueType type, const std::string& valueStr);
    std::vector<ScanResult> aobScan(const std::string& pattern);
    
    std::vector<ScanResult> getResults() const { 
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        return m_results; 
    }
    void clearResults();

private:
    pid_t m_pid = -1;
    std::vector<ScanResult> m_results;
    mutable std::mutex m_resultsMutex;
    
    std::atomic<bool> m_isScanning{false};
    std::atomic<float> m_progress{0.0f};
    
    ssize_t readRaw(uintptr_t address, void* buffer, size_t size);
    ssize_t writeRaw(uintptr_t address, const void* buffer, size_t size);

    void scanRegionChunked(const MemoryRegion& region, ValueType type, uint32_t targetVal, float targetFloat, const std::string& targetStr, std::vector<ScanResult>& localResults);
};
