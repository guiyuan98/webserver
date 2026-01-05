#pragma once
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

// 日志级别
enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

// 简单的日志类
class Logger {
  private:
    static std::mutex log_mutex_;
    static LogLevel min_level_;

    static const char *level_to_string(LogLevel level) { // 将日志级别转换为字符串
        switch (level) {
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO ";
        case LogLevel::WARN:
            return "WARN ";
        case LogLevel::ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
        }
    }

    static std::string get_timestamp() { // 获取当前时间戳
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) %
                  1000;

        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        oss << "." << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }

  public:
    static void set_min_level(LogLevel level) { // 设置最小日志级别
        min_level_ = level;
    }

    static void log(LogLevel level, const std::string &message) { // 记录日志
        if (level < min_level_) {
            return;
        }

        std::lock_guard<std::mutex> lock(log_mutex_);
        std::cout << "[" << get_timestamp() << "] "
                  << "[" << level_to_string(level) << "] "
                  << message << std::endl;
    }

    static void debug(const std::string &message) { // 记录调试日志
        log(LogLevel::DEBUG, message);
    }

    static void info(const std::string &message) { // 记录信息日志
        log(LogLevel::INFO, message);
    }

    static void warn(const std::string &message) { // 记录警告日志
        log(LogLevel::WARN, message);
    }

    static void error(const std::string &message) { // 记录错误日志
        log(LogLevel::ERROR, message);
    }
};

// 静态成员初始化
std::mutex Logger::log_mutex_;
LogLevel Logger::min_level_ = LogLevel::INFO;
