#define _GNU_SOURCE
#include "mem_scanner.hpp"
#include <sys/uio.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <vector>
#include <future>
#include <algorithm>

MemScanner::MemScanner() : m_isScanning(false), m_progress(0.0f) {}

MemScanner::~MemScanner() {
    detach();
}

bool MemScanner::attach(pid_t pid) {
    std::ifstream maps_file("/proc/" + std::to_string(pid) + "/maps");
    if (!maps_file.is_open()) return false;

    m_pid = pid;
    return true;
}

void MemScanner::detach() {
    m_pid = -1;
    clearResults();
}

void MemScanner::clearResults() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    m_results.clear();
}

struct Settings {
    int alignment = 4;
    bool darkMode = true;
    bool scanRead = true;
    bool scanWrite = false;
    bool scanExec = false;
    bool excludeKernel = true;
    int maxResults = 10000;
};
extern Settings g_settings;

std::vector<MemoryRegion> MemScanner::getRegions() {
    std::vector<MemoryRegion> regions;
    if (m_pid == -1) return regions;

    std::ifstream maps_file("/proc/" + std::to_string(m_pid) + "/maps");
    std::string line;

    while (std::getline(maps_file, line)) {
        std::istringstream iss(line);
        std::string range, perms, offset, dev, inode, pathname;
        iss >> range >> perms >> offset >> dev >> inode;

        size_t dash = range.find('-');
        uintptr_t start = std::stoull(range.substr(0, dash), nullptr, 16);
        uintptr_t end = std::stoull(range.substr(dash + 1), nullptr, 16);

        bool permMatch = (g_settings.scanRead && perms.find('r') != std::string::npos) ||
        (g_settings.scanWrite && perms.find('w') != std::string::npos) ||
        (g_settings.scanExec && perms.find('x') != std::string::npos);

        if (permMatch &&
            !(g_settings.excludeKernel && start >= 0xffffffff80000000) &&
            pathname.find("[vvar]") == std::string::npos &&
            pathname.find("[vdso]") == std::string::npos) {
            regions.push_back({start, end, perms, pathname});
            }
    }
    return regions;
}

ssize_t MemScanner::readRaw(uintptr_t address, void* buffer, size_t size) {
    if (m_pid == -1) return -1;
    struct iovec local[1];
    struct iovec remote[1];
    local[0].iov_base = buffer;
    local[0].iov_len = size;
    remote[0].iov_base = (void*)address;
    remote[0].iov_len = size;
    return process_vm_readv(m_pid, local, 1, remote, 1, 0);
}

ssize_t MemScanner::writeRaw(uintptr_t address, const void* buffer, size_t size) {
    if (m_pid == -1) return -1;
    
    std::string memPath = "/proc/" + std::to_string(m_pid) + "/mem";
    int fd = open(memPath.c_str(), O_WRONLY);
    if (fd == -1) {
        // Fallback or error
        return -1;
    }

    ssize_t written = pwrite(fd, buffer, size, (off_t)address);
    close(fd);
    return written;
}

template<typename T>
T MemScanner::readMemory(uintptr_t address) {
    T buffer;
    if (readRaw(address, &buffer, sizeof(T)) != sizeof(T)) {
        std::memset(&buffer, 0, sizeof(T));
    }
    return buffer;
}

template<typename T>
bool MemScanner::writeMemory(uintptr_t address, T value) {
    return writeRaw(address, &value, sizeof(T)) == sizeof(T);
}

template uint8_t MemScanner::readMemory<uint8_t>(uintptr_t);
template uint16_t MemScanner::readMemory<uint16_t>(uintptr_t);
template uint32_t MemScanner::readMemory<uint32_t>(uintptr_t);
template uint64_t MemScanner::readMemory<uint64_t>(uintptr_t);
template float MemScanner::readMemory<float>(uintptr_t);
template double MemScanner::readMemory<double>(uintptr_t);

template bool MemScanner::writeMemory<uint8_t>(uintptr_t, uint8_t);
template bool MemScanner::writeMemory<uint16_t>(uintptr_t, uint16_t);
template bool MemScanner::writeMemory<uint32_t>(uintptr_t, uint32_t);
template bool MemScanner::writeMemory<uint64_t>(uintptr_t, uint64_t);
template bool MemScanner::writeMemory<float>(uintptr_t, float);
template bool MemScanner::writeMemory<double>(uintptr_t, double);

std::vector<MemScanner::AOBByte> MemScanner::parseAOB(const std::string& pattern) {
    std::vector<AOBByte> bytes;
    std::stringstream ss(pattern);
    std::string hex;
    while (ss >> hex) {
        if (hex == "??" || hex == "?") {
            bytes.push_back({0, true});
        } else {
            try {
                bytes.push_back({(uint8_t)std::stoul(hex, nullptr, 16), false});
            } catch (...) {}
        }
    }
    return bytes;
}

bool MemScanner::patch(uintptr_t address, const std::string& pattern) {
    auto aob = parseAOB(pattern);
    if (aob.empty()) return false;
    
    for (size_t i = 0; i < aob.size(); ++i) {
        if (!aob[i].isWildcard) {
            if (!writeMemory<uint8_t>(address + i, aob[i].value)) {
                return false;
            }
        }
    }
    return true;
}

const size_t CHUNK_SIZE = 1024 * 1024; // 1MB

void MemScanner::scanRegionChunked(const MemoryRegion& region, ValueType type, uint32_t targetVal, float targetFloat, const std::string& targetStr, std::vector<ScanResult>& localResults) {
    size_t regionSize = region.end - region.start;
    
    if (type == ValueType::AOB) {
        auto aob = parseAOB(targetStr);
        if (aob.empty()) return;

        // Optimization: Find first non-wildcard byte to use memchr
        size_t firstFixedIdx = 0;
        while (firstFixedIdx < aob.size() && aob[firstFixedIdx].isWildcard) {
            firstFixedIdx++;
        }

        std::vector<uint8_t> buffer(CHUNK_SIZE + aob.size());
        for (size_t offset = 0; offset < regionSize; offset += CHUNK_SIZE) {
            size_t bytesToRead = std::min(CHUNK_SIZE, regionSize - offset);
            size_t readSize = std::min(bytesToRead + aob.size() - 1, regionSize - offset);
            
            if (readRaw(region.start + offset, buffer.data(), readSize) > 0) {
                if (firstFixedIdx < aob.size()) {
                    // Optimized path: use memchr to find first fixed byte
                    uint8_t firstByte = aob[firstFixedIdx].value;
                    uint8_t* ptr = buffer.data();
                    uint8_t* endPtr = buffer.data() + bytesToRead;

                    while (ptr < endPtr) {
                        uint8_t* candidate = (uint8_t*)std::memchr(ptr, firstByte, endPtr - ptr);
                        if (!candidate) break;

                        // Calculate potential start of match
                        intptr_t matchStartIdx = (candidate - buffer.data()) - firstFixedIdx;
                        if (matchStartIdx >= 0 && (size_t)matchStartIdx + aob.size() <= readSize) {
                            bool match = true;
                            for (size_t j = 0; j < aob.size(); ++j) {
                                if (!aob[j].isWildcard && buffer[matchStartIdx + j] != aob[j].value) {
                                    match = false;
                                    break;
                                }
                            }
                            if (match) localResults.push_back({region.start + offset + (size_t)matchStartIdx});
                        }
                        ptr = candidate + 1;
                    }
                } else {
                    // Fallback for all-wildcard pattern (rare)
                    for (size_t i = 0; i < bytesToRead; ++i) {
                        if (i + aob.size() > readSize) break;
                        localResults.push_back({region.start + offset + i});
                    }
                }
            }
        }
        return;
    }

    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) {
        numThreads = 4;
    }
    
    size_t totalChunks = (regionSize + CHUNK_SIZE - 1) / CHUNK_SIZE;
    
    if (numThreads <= 1 || totalChunks < numThreads * 2) {
        std::vector<uint8_t> buffer(CHUNK_SIZE + 8); 
        for (size_t offset = 0; offset < regionSize; offset += CHUNK_SIZE) {
            size_t bytesToRead = std::min(CHUNK_SIZE, regionSize - offset);
            size_t readSize = std::min(bytesToRead + 1, regionSize - offset);
            if (readRaw(region.start + offset, buffer.data(), readSize) > 0) {
                size_t valSize = (type == ValueType::String) ? targetStr.length() : 4;
                if (bytesToRead < valSize) continue; 

                for (size_t i = 0; i <= bytesToRead - valSize; ++i) {
                    bool match = false;
                    if (type == ValueType::FourBytes) {
                        uint32_t val;
                        std::memcpy(&val, &buffer[i], 4);
                        if (val == targetVal) match = true;
                    } else if (type == ValueType::Float) {
                        float val;
                        std::memcpy(&val, &buffer.data()[i], 4);
                        if (val == targetFloat) match = true;
                    } else if (type == ValueType::String) {
                        if (std::memcmp(&buffer[i], targetStr.c_str(), targetStr.length()) == 0) {
                            size_t afterIdx = i + targetStr.length();
                            if (afterIdx >= readSize || buffer[afterIdx] == '\0') { 
                                match = true;
                            }
                        }
                    }

                    if (match) {
                        localResults.push_back({region.start + offset + i});
                    }
                }
            }
        }
        return;
    }

    std::vector<std::future<std::vector<ScanResult>>> futures;
    size_t subChunkSize = totalChunks / numThreads;
    size_t remainder = totalChunks % numThreads;
    size_t currentOffset = 0;

    for (unsigned int i = 0; i < numThreads; ++i) {
        size_t chunksForThisThread = subChunkSize + (i < remainder ? 1 : 0);
        size_t startOffset = currentOffset;
        size_t endOffset = startOffset + chunksForThisThread * CHUNK_SIZE;
        endOffset = std::min(endOffset, regionSize); 
        
        if (startOffset >= endOffset) continue;

        futures.push_back(std::async(std::launch::async, [this, region, regionSize, type, targetVal, targetFloat, targetStr, startOffset, endOffset]() {
            std::vector<ScanResult> threadLocalResults;
            size_t currentOffsetInThread = startOffset;
            std::vector<uint8_t> buffer(CHUNK_SIZE + 8); 

            while(currentOffsetInThread < endOffset) {
                size_t bytesToRead = std::min(CHUNK_SIZE, endOffset - currentOffsetInThread);
                size_t readSize = std::min(bytesToRead + 1, regionSize - currentOffsetInThread); 

                if (readRaw(region.start + currentOffsetInThread, buffer.data(), readSize) > 0) {
                    size_t valSize = (type == ValueType::String) ? targetStr.length() : 4;
                    if (bytesToRead < valSize) continue;

                    for (size_t i = 0; i <= bytesToRead - valSize; ++i) {
                        bool match = false;
                        if (type == ValueType::FourBytes) {
                            uint32_t val;
                            std::memcpy(&val, &buffer[i], 4);
                            if (val == targetVal) match = true;
                        } else if (type == ValueType::Float) {
                            float val;
                            std::memcpy(&val, &buffer.data()[i], 4);
                            if (val == targetFloat) match = true;
                        } else if (type == ValueType::String) {
                            if (std::memcmp(&buffer[i], targetStr.c_str(), targetStr.length()) == 0) {
                                size_t afterIdx = i + targetStr.length();
                                if (afterIdx >= readSize || buffer[afterIdx] == '\0') { 
                                    match = true;
                                }
                            }
                        }

                        if (match) {
                            threadLocalResults.push_back({region.start + currentOffsetInThread + i});
                        }
                    }
                }
                currentOffsetInThread += CHUNK_SIZE;
            }
            return threadLocalResults;
        }));
        currentOffset = endOffset;
    }

    for (auto& future : futures) {
        auto results = future.get();
        localResults.insert(localResults.end(), results.begin(), results.end());
    }
}

std::string MemScanner::readString(uintptr_t address, size_t maxLength) {
    std::vector<char> buffer(maxLength + 1);
    ssize_t n = readRaw(address, buffer.data(), maxLength);
    if (n <= 0) return "";
    buffer[n] = '\0';
    return std::string(buffer.data());
}

bool MemScanner::writeString(uintptr_t address, const std::string& value) {
    return writeRaw(address, value.c_str(), value.length()) == (ssize_t)value.length();
}

std::vector<ScanResult> MemScanner::aobScan(const std::string& pattern) {
    auto aob = parseAOB(pattern);
    if (aob.empty()) return {};

    auto regions = getRegions();
    std::vector<std::future<std::vector<ScanResult>>> futures;

    for (const auto& region : regions) {
        futures.push_back(std::async(std::launch::async, [this, region, aob]() {
            std::vector<ScanResult> local;
            size_t regionSize = region.end - region.start;
            std::vector<uint8_t> buffer(CHUNK_SIZE + aob.size());

            for (size_t offset = 0; offset < regionSize; offset += CHUNK_SIZE) {
                size_t bytesToRead = std::min(CHUNK_SIZE, regionSize - offset);
                size_t readSize = std::min(bytesToRead + aob.size() - 1, regionSize - offset);
                
                if (readRaw(region.start + offset, buffer.data(), readSize) > 0) {
                    for (size_t i = 0; i < bytesToRead; ++i) {
                        if (i + aob.size() > readSize) break;
                        
                        bool match = true;
                        for (size_t j = 0; j < aob.size(); ++j) {
                            if (!aob[j].isWildcard && buffer[i + j] != aob[j].value) {
                                match = false;
                                break;
                            }
                        }
                        if (match) {
                            local.push_back({region.start + offset + i});
                        }
                    }
                }
            }
            return local;
        }));
    }

    std::vector<ScanResult> allResults;
    for (auto& f : futures) {
        auto res = f.get();
        allResults.insert(allResults.end(), res.begin(), res.end());
    }
    return allResults;
}

void MemScanner::firstScan(ValueType type, const std::string& valueStr) {
    if (m_isScanning) return;
    m_isScanning = true;
    m_progress = 0.0f;

    std::thread([this, type, valueStr]() {
        clearResults();
        auto regions = getRegions();

        uint32_t targetVal = 0;
        float targetFloat = 0.0f;
        try {
            if (type == ValueType::FourBytes) targetVal = (uint32_t)std::stoul(valueStr);
            else if (type == ValueType::Float) targetFloat = std::stof(valueStr);
        } catch (...) {
            if (type != ValueType::String && type != ValueType::AOB) {
                m_isScanning = false;
                return;
            }
        }

        std::vector<std::future<std::vector<ScanResult>>> futures;
        for (const auto& region : regions) {
            futures.push_back(std::async(std::launch::async, [this, region, type, targetVal, targetFloat, valueStr]() {
                std::vector<ScanResult> local;
                scanRegionChunked(region, type, targetVal, targetFloat, valueStr, local);
                return local;
            }));
        }

        std::vector<ScanResult> allResults;
        for (size_t i = 0; i < futures.size(); ++i) {
            auto results = futures[i].get();
            allResults.insert(allResults.end(), results.begin(), results.end());
            m_progress = (float)(i + 1) / futures.size();
        }

        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            m_results = std::move(allResults);
        }

        m_isScanning = false;
        m_progress = 1.0f;
    }).detach();
}

void MemScanner::nextScan(ValueType type, const std::string& valueStr) {
    if (m_isScanning) return;
    m_isScanning = true;
    m_progress = 0.0f;

    std::thread([this, type, valueStr]() {
        uint32_t targetVal = 0;
        float targetFloat = 0.0f;
        try {
            if (type == ValueType::FourBytes) targetVal = (uint32_t)std::stoul(valueStr);
            if (type == ValueType::Float) targetFloat = std::stof(valueStr);
        } catch (...) {
            m_isScanning = false;
            return;
        }

        std::vector<ScanResult> currentResults;
        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            currentResults = m_results;
        }

        if (currentResults.empty()) {
            m_isScanning = false;
            m_progress = 1.0f;
            return;
        }

        std::vector<ScanResult> newResults;
        std::vector<std::future<std::vector<ScanResult>>> futures;

        unsigned int numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) {
            numThreads = 4;
        }
        numThreads = std::min(numThreads, (unsigned int)currentResults.size());
        
        size_t chunkSize = currentResults.size() / numThreads;
        size_t remainder = currentResults.size() % numThreads;
        size_t startIndex = 0;

        for (unsigned int i = 0; i < numThreads; ++i) {
            size_t currentChunkSize = chunkSize + (i < remainder ? 1 : 0);
            size_t endIndex = startIndex + currentChunkSize;

            if (currentChunkSize == 0) continue;

            futures.push_back(std::async(std::launch::async, [this, &currentResults, type, targetVal, targetFloat, valueStr, startIndex, endIndex]() {
                std::vector<ScanResult> localResults;
                for (size_t j = startIndex; j < endIndex; ++j) {
                    const auto& res = currentResults[j];
                    bool match = false;
                    if (type == ValueType::FourBytes) {
                        uint32_t val = readMemory<uint32_t>(res.address);
                        if (val == targetVal) match = true;
                    } else if (type == ValueType::Float) {
                        float val = readMemory<float>(res.address);
                        if (val == targetFloat) match = true;
                    } else if (type == ValueType::String) {
                        std::string val = readString(res.address, valueStr.length() + 1);
                        if (val.length() >= valueStr.length() &&
                            val.substr(0, valueStr.length()) == valueStr &&
                            (val.length() == valueStr.length() || val[valueStr.length()] == '\0')) {
                            match = true;
                        }
                    }
                    if (match) {
                        localResults.push_back(res);
                    }
                }
                return localResults;
            }));
            startIndex = endIndex;
        }

        for (size_t i = 0; i < futures.size(); ++i) {
            auto results = futures[i].get();
            newResults.insert(newResults.end(), results.begin(), results.end());
            m_progress = (float)(i + 1) / futures.size(); 
        }

        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            m_results = std::move(newResults);
        }

        m_isScanning = false;
        m_progress = 1.0f;
    }).detach();
}
