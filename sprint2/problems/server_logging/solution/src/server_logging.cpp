#include "server_logging.h"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/log/attributes/value_extraction.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/keywords/format.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

#include <iostream>
#include <mutex>

namespace server_logging {

namespace logging = boost::log;
namespace expr = boost::log::expressions;
namespace posix_time = boost::posix_time;

BOOST_LOG_ATTRIBUTE_KEYWORD(additional_data, "AdditionalData", json::value)
BOOST_LOG_ATTRIBUTE_KEYWORD(timestamp, "TimeStamp", posix_time::ptime)

namespace {

class JsonFormatter {
public:
    void operator()(logging::record_view const& record, logging::formatting_ostream& output) const {
        json::object object;
        if (const auto time_value = record[timestamp]) {
            object["timestamp"] = posix_time::to_iso_extended_string(time_value.get());
        } else {
            object["timestamp"] = "";
        }
        if (const auto data = record[additional_data]) {
            object["data"] = data.get();
        } else {
            object["data"] = json::object{};
        }
        if (const auto message = record[expr::smessage]) {
            object["message"] = message.get();
        } else {
            object["message"] = "";
        }
        output << json::serialize(object) << '\n';
    }
};

void EnsureInitialized() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        auto sink = logging::add_console_log(std::cout);
        sink->set_formatter(JsonFormatter{});
        sink->locked_backend()->auto_flush(true);
        logging::add_common_attributes();
    });
}

}  // namespace

void Init() {
    EnsureInitialized();
}

void LogRequest(std::string_view ip, std::string_view uri, std::string_view method) {
    EnsureInitialized();
    json::object data{{"ip", std::string(ip)}, {"URI", std::string(uri)}, {"method", std::string(method)}};
    BOOST_LOG_TRIVIAL(info) << logging::add_value(additional_data, json::value(std::move(data)))
                            << "request received";
}

void LogResponse(std::string_view ip, int code, long long response_time, const json::value& content_type) {
    EnsureInitialized();
    json::object data{{"ip", std::string(ip)},
                      {"response_time", response_time},
                      {"code", code},
                      {"content_type", content_type}};
    BOOST_LOG_TRIVIAL(info) << logging::add_value(additional_data, json::value(std::move(data)))
                            << "response sent";
}

void LogError(int code, std::string_view text, std::string_view where) {
    EnsureInitialized();
    json::object data{{"code", code}, {"text", std::string(text)}, {"where", std::string(where)}};
    BOOST_LOG_TRIVIAL(error) << logging::add_value(additional_data, json::value(std::move(data))) << "error";
}

void LogStarted(int port, std::string_view address) {
    EnsureInitialized();
    json::object data{{"port", port}, {"address", std::string(address)}};
    BOOST_LOG_TRIVIAL(info) << logging::add_value(additional_data, json::value(std::move(data))) << "server started";
}

void LogExited(int code, std::string_view exception) {
    EnsureInitialized();
    json::object data{{"code", code}};
    if (!exception.empty()) {
        data["exception"] = std::string(exception);
    }
    BOOST_LOG_TRIVIAL(info) << logging::add_value(additional_data, json::value(std::move(data))) << "server exited";
}

}  // namespace server_logging
