#include "FileSystemGuard.hpp"

#include <iostream>

std::mutex FileSystemGuard::global_lock_;

FileSystemGuard::FileSystemGuard(const std::filesystem::path &targetDir) {
    global_lock_.lock();
    try {
        oldDir_ = std::filesystem::current_path();
        std::filesystem::current_path(targetDir);
        switched_ = true;
    } catch (std::exception &e) {
        global_lock_.unlock();
        throw;
    }
}

FileSystemGuard::~FileSystemGuard() noexcept {
    if (switched_) {
        try {
            std::filesystem::current_path(oldDir_);
        } catch (std::exception &e) {
            std::cerr << e.what() << std::endl;
        }
        switched_ = false;
        oldDir_ = "";
    }
    global_lock_.unlock();
}
