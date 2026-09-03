#pragma once

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <string_view>

namespace server_logging {

namespace json = boost::json;

void Init();
void LogRequest(std::string_view ip, std::string_view uri, std::string_view method);
void LogResponse(std::string_view ip, int code, long long response_time, const json::value& content_type);
void LogError(int code, std::string_view text, std::string_view where);
void LogStarted(int port, std::string_view address);
void LogExited(int code, std::string_view exception = {});

}  // namespace server_logging
