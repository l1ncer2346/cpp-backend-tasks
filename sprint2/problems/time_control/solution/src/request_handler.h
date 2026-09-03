#pragma once

#include "sdk.h"

#include <boost/json.hpp>
#include <boost/beast/http.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "model.h"

namespace http_handler {

namespace http = boost::beast::http;
namespace json = boost::json;

class RequestHandler {
public:
    RequestHandler(model::Game& game, std::filesystem::path www_root = {}, bool automatic_ticks = false,
                   bool randomize_spawn_points = false)
        : game_(game)
        , www_root_(std::move(www_root))
        , automatic_ticks_(automatic_ticks)
        , randomize_spawn_points_(randomize_spawn_points) {
    }

    RequestHandler(const RequestHandler&) = delete;
    RequestHandler& operator=(const RequestHandler&) = delete;

    template <typename Body, typename Allocator, typename Send>
    void operator()(http::request<Body, http::basic_fields<Allocator>>&& request, Send&& send,
                    [[maybe_unused]] std::string client_address = {}) {
        const bool is_head = request.method() == http::verb::head;
#ifdef SERVER_LOGGING
        const auto started = std::chrono::steady_clock::now();
        LogRequest(client_address, request.target(), request.method_string());
#endif

        StringResponse response = HandleRequest(request, std::string(request.target()));
        if (is_head) {
            // Content-Length is deliberately retained: HEAD has the same metadata as GET,
            // but no response body on the wire.
            response.body().clear();
        }

#ifdef SERVER_LOGGING
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
        LogResponse(client_address, response, elapsed);
#endif

        send(std::move(response));
    }

private:
    using StringResponse = http::response<http::string_body>;

    StringResponse HandleRequest(const http::request<http::string_body>& request, const std::string& target) const;
    StringResponse HandleApiRequest(const http::request<http::string_body>& request, const std::string& target) const;
    StringResponse HandleStaticRequest(const http::request<http::string_body>& request,
                                       const std::string& target) const;

    StringResponse MakeMapsListResponse(unsigned version, bool keep_alive) const;
    StringResponse MakeMapResponse(std::string_view map_id, unsigned version, bool keep_alive) const;
    StringResponse MakeJoinResponse(const http::request<http::string_body>& request) const;
    StringResponse MakePlayersResponse(const http::request<http::string_body>& request) const;
    StringResponse MakeStateResponse(const http::request<http::string_body>& request) const;
    StringResponse MakeActionResponse(const http::request<http::string_body>& request) const;
    StringResponse MakeTickResponse(const http::request<http::string_body>& request) const;

    static json::value MapToJson(const model::Map& map);
    static json::value PlayerStateToJson(const model::PlayerState& player);
    static std::string DirectionToString(model::Direction direction);
    static bool IsJsonContentType(const http::request<http::string_body>& request);
    static std::optional<std::string> GetToken(const http::request<http::string_body>& request);

    static StringResponse MakeJsonResponse(http::status status, const json::value& value, unsigned version,
                                            bool keep_alive);
    static StringResponse MakeErrorResponse(http::status status, std::string_view code, std::string_view message,
                                             unsigned version, bool keep_alive);
    static StringResponse MakeMethodError(std::string_view allow, unsigned version, bool keep_alive);
    static StringResponse MakePlainResponse(http::status status, std::string_view body, unsigned version,
                                             bool keep_alive);

    StringResponse MakeStaticResponse(const std::filesystem::path& path, unsigned version, bool keep_alive) const;
    std::filesystem::path ResolveStaticPath(std::string_view target, bool& outside_root) const;
    static std::string DecodeUrl(std::string_view value);

#ifdef SERVER_LOGGING
    static void LogRequest(std::string_view ip, std::string_view target, std::string_view method);
    static void LogResponse(std::string_view ip, const StringResponse& response, long long response_time);
#endif

    model::Game& game_;
    std::filesystem::path www_root_;
    bool automatic_ticks_ = false;
    bool randomize_spawn_points_ = true;
};

}  // namespace http_handler
