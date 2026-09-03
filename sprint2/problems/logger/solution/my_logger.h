#pragma once

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace std::literals;

#define LOG(...) Logger::GetInstance().Log(__VA_ARGS__)

class Logger {
private:
    using TimePoint = std::chrono::system_clock::time_point;

    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    TimePoint GetTimeLocked() const {
        return manual_ts_.value_or(std::chrono::system_clock::now());
    }

    static std::tm GetLocalTime(TimePoint time) {
        const std::time_t calendar_time = std::chrono::system_clock::to_time_t(time);
        const std::tm* local_time = std::localtime(&calendar_time);
        if (!local_time) {
            throw std::runtime_error("Failed to convert time");
        }
        return *local_time;
    }

    std::string GetFileTimeStampLocked(TimePoint time) const {
        const std::tm local_time = GetLocalTime(time);
        std::ostringstream output;
        output << std::put_time(&local_time, "%Y_%m_%d");
        return output.str();
    }

    void OpenFileLocked(const std::string& date) {
        if (date == opened_date_ && log_file_.is_open()) {
            return;
        }
        if (log_file_.is_open()) {
            log_file_.flush();
            log_file_.close();
        }
        log_file_.open("/var/log/sample_log_" + date + ".log", std::ios::out | std::ios::app);
        if (!log_file_) {
            throw std::runtime_error("Failed to open log file");
        }
        opened_date_ = date;
    }

public:
    static Logger& GetInstance() {
        static Logger logger;
        return logger;
    }

    template <class... Ts>
    void Log(const Ts&... args) {
        std::lock_guard lock(mutex_);
        const TimePoint timestamp = GetTimeLocked();
        const std::tm local_time = GetLocalTime(timestamp);
        const std::string date = GetFileTimeStampLocked(timestamp);
        OpenFileLocked(date);

        std::ostringstream message;
        message << std::put_time(&local_time, "%F %T") << ": ";
        if constexpr (sizeof...(Ts) > 0) {
            (message << ... << args);
        }
        message << '\n';
        log_file_ << message.str();
        log_file_.flush();
    }

    void SetTimestamp(TimePoint timestamp) {
        std::lock_guard lock(mutex_);
        manual_ts_ = timestamp;
    }

private:
    std::optional<TimePoint> manual_ts_;
    std::mutex mutex_;
    std::ofstream log_file_;
    std::string opened_date_;
};
