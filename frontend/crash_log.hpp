#pragma once
#include <filesystem>
class CrashLog {
public:
    explicit CrashLog(const std::filesystem::path& file);
    ~CrashLog();
    CrashLog(const CrashLog&) = delete;
    CrashLog& operator=(const CrashLog&) = delete;
};
