#pragma once
#include <filesystem>
#include <mutex>

// a simple filesystem guard to chdir into the KIM TEMP file folder
// then cd back on completion. this is needed as KUSP tries to capture
// the python env as close to dev time as possible so that we have no
// extra barrier for deploying. Acting like a mutex, init it in a scope
// initialize the kusp model, then init out. -ves will be that model now have
// to be in memory.
// for persistant access to filesystem (for any reason) use CWD and relative paths
// in the model

class FileSystemGuard {
public:
    explicit FileSystemGuard(const std::filesystem::path& targetDir);

    // On destruction, restore directory
    ~FileSystemGuard() noexcept;
    // Non-copyable
    FileSystemGuard(const FileSystemGuard&) = delete;
    FileSystemGuard& operator=(const FileSystemGuard&) = delete;

    // Non-Movable
    FileSystemGuard(FileSystemGuard&&) = delete;
    FileSystemGuard& operator=(FileSystemGuard&&) = delete;

private:
    std::filesystem::path oldDir_;
    bool switched_ = false;
    static std::mutex global_lock_;
};
