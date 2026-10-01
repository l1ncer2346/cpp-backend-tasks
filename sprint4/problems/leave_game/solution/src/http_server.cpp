#include "http_server.h"

#ifdef SERVER_LOGGING
#include "server_logging.h"
#endif

#include <boost/asio/dispatch.hpp>

#include <chrono>
#include <iostream>

namespace http_server {

void ReportError(beast::error_code ec, std::string_view where) {
#ifdef SERVER_LOGGING
    server_logging::LogError(ec.value(), ec.message(), where);
#else
    std::cerr << where << ": " << ec.message() << std::endl;
#endif
}

void SessionBase::Run() {
    net::dispatch(stream_.get_executor(), beast::bind_front_handler(&SessionBase::Read, GetSharedThis()));
}

void SessionBase::Read() {
    request_ = {};
    stream_.expires_after(std::chrono::seconds(30));
    http::async_read(stream_, buffer_, request_,
                     beast::bind_front_handler(&SessionBase::OnRead, GetSharedThis()));
}

void SessionBase::OnRead(beast::error_code ec, [[maybe_unused]] std::size_t bytes_read) {
    if (ec == http::error::end_of_stream) {
        return Close();
    }
    if (ec) {
        ReportError(ec, "read");
        return;
    }
    HandleRequest(std::move(request_));
}

void SessionBase::Close() {
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
    if (ec && ec != beast::errc::not_connected) {
        ReportError(ec, "write");
    }
}

void SessionBase::OnWrite(bool close, beast::error_code ec, [[maybe_unused]] std::size_t bytes_written) {
    if (ec) {
        ReportError(ec, "write");
        return;
    }
    if (close) {
        return Close();
    }
    Read();
}

}  // namespace http_server
