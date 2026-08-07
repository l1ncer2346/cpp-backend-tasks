#pragma once
#include <boost/json.hpp>
#include <string>
#include <string_view>

#include "http_server.h"
#include "model.h"

namespace http_handler {
namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;

class RequestHandler {
public:
    explicit RequestHandler(model::Game& game)
        : game_{game} {
    }

    RequestHandler(const RequestHandler&) = delete;
    RequestHandler& operator=(const RequestHandler&) = delete;

    template <typename Body, typename Allocator, typename Send>
    void operator()(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        const bool is_head = req.method() == http::verb::head;

        StringResponse response = (req.method() == http::verb::get || is_head)
                                       ? HandleApiRequest(std::string(req.target()), req.version(), req.keep_alive())
                                       : MakeErrorResponse(http::status::bad_request, "badRequest", "Bad request",
                                                            req.version(), req.keep_alive());
        if (is_head) {
            // Тело ответа на HEAD-запрос должно быть пустым, а Content-Length - таким же, как в ответе на GET
            response.body().clear();
        }

        send(std::move(response));
    }

private:
    using StringResponse = http::response<http::string_body>;

    StringResponse HandleApiRequest(const std::string& target, unsigned version, bool keep_alive) const;
    StringResponse MakeMapsListResponse(unsigned version, bool keep_alive) const;
    StringResponse MakeMapResponse(const std::string& map_id, unsigned version, bool keep_alive) const;

    static json::value MapToJson(const model::Map& map);
    static StringResponse MakeJsonResponse(http::status status, const json::value& value, unsigned version,
                                            bool keep_alive);
    static StringResponse MakeErrorResponse(http::status status, std::string_view code, std::string_view message,
                                             unsigned version, bool keep_alive);

    model::Game& game_;
};

}  // namespace http_handler
