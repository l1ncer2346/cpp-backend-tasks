#include "sdk.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>

#ifdef COMMAND_LINE_MODE
#include <boost/program_options.hpp>
#endif

#ifdef SERVER_LOGGING
#include "server_logging.h"
#endif

#include "http_server.h"
#include "json_loader.h"
#include "request_handler.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
#include <thread>
#include <vector>

namespace net = boost::asio;
namespace sys = boost::system;

namespace {

struct Options {
    std::filesystem::path config_file;
    std::filesystem::path www_root;
    std::optional<int> tick_period;
    bool randomize_spawn_points = false;
};

#ifdef COMMAND_LINE_MODE
Options ParseOptions(int argc, const char* argv[]) {
    namespace po = boost::program_options;
    po::options_description description("Allowed options");
    description.add_options()
        ("help,h", "produce help message")
        ("tick-period,t", po::value<int>()->value_name("milliseconds"), "set tick period")
        ("config-file,c", po::value<std::string>()->value_name("file")->required(), "set config file path")
        ("www-root,w", po::value<std::string>()->value_name("dir"), "set static files root")
        ("randomize-spawn-points", "spawn dogs at random positions");

    po::variables_map variables;
    try {
        po::store(po::parse_command_line(argc, argv, description), variables);
        if (variables.contains("help")) {
            std::cout << description << std::endl;
            std::exit(EXIT_SUCCESS);
        }
        po::notify(variables);
    } catch (const po::error& error) {
        std::cerr << error.what() << std::endl << description << std::endl;
        throw std::invalid_argument("Invalid command line");
    }

    Options options;
    options.config_file = variables["config-file"].as<std::string>();
    if (variables.contains("www-root")) {
        options.www_root = variables["www-root"].as<std::string>();
    }
    if (variables.contains("tick-period")) {
        options.tick_period = variables["tick-period"].as<int>();
        if (*options.tick_period <= 0) {
            throw std::invalid_argument("Tick period must be positive");
        }
    }
    options.randomize_spawn_points = variables.contains("randomize-spawn-points");
    return options;
}
#else
Options ParseOptions(int argc, const char* argv[]) {
    if (argc < 2 || argc > 3) {
        throw std::invalid_argument("Usage: game_server <game-config-json> [static-files-root]");
    }
    Options options;
    options.config_file = argv[1];
    if (argc == 3) {
        options.www_root = argv[2];
    }
    // This task fixes the spawn point to make movement deterministic for tests.
    options.randomize_spawn_points = false;
    return options;
}
#endif

class AutomaticTicker final : public std::enable_shared_from_this<AutomaticTicker> {
public:
    AutomaticTicker(net::io_context& ioc, model::Game& game, std::chrono::milliseconds period)
        : timer_(ioc)
        , game_(game)
        , period_(period) {
    }

    void Start() {
        timer_.expires_after(period_);
        timer_.async_wait([self = shared_from_this()](sys::error_code ec) {
            if (ec) {
                return;
            }
            self->game_.Tick(self->period_.count());
            self->Start();
        });
    }

    void Stop() {
        timer_.cancel();
    }

private:
    net::steady_timer timer_;
    model::Game& game_;
    std::chrono::milliseconds period_;
};

template <typename Function>
void RunWorkers(unsigned count, Function&& function) {
    count = std::max(1u, count);
    std::vector<std::jthread> workers;
    workers.reserve(count > 0 ? count - 1 : 0);
    while (--count) {
        workers.emplace_back(function);
    }
    function();
}

}  // namespace

int main(int argc, const char* argv[]) {
#ifdef SERVER_LOGGING
    server_logging::Init();
#endif
    try {
        const Options options = ParseOptions(argc, argv);
        model::Game game = json_loader::LoadGame(options.config_file);

        const unsigned num_threads = std::max(1u, std::thread::hardware_concurrency());
        net::io_context ioc(num_threads);
        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](const sys::error_code& ec, int) {
            if (!ec) {
                ioc.stop();
            }
        });

        http_handler::RequestHandler handler{game, options.www_root, options.tick_period.has_value(),
                                             options.randomize_spawn_points};
        const auto address = net::ip::make_address("0.0.0.0");
        constexpr net::ip::port_type port = 8080;
        http_server::ServeHttp(ioc, {address, port}, [&handler](auto&& request, auto&& send, std::string address) {
            handler(std::forward<decltype(request)>(request), std::forward<decltype(send)>(send),
                    std::move(address));
        });

        std::shared_ptr<AutomaticTicker> ticker;
        if (options.tick_period) {
            ticker = std::make_shared<AutomaticTicker>(ioc, game, std::chrono::milliseconds(*options.tick_period));
            ticker->Start();
        }

#ifdef SERVER_LOGGING
        server_logging::LogStarted(port, "0.0.0.0");
#else
        std::cout << "Server has started..." << std::endl;
#endif

        RunWorkers(num_threads, [&ioc] {
            ioc.run();
        });

        if (ticker) {
            ticker->Stop();
        }
#ifdef SERVER_LOGGING
        server_logging::LogExited(EXIT_SUCCESS);
#endif
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
#ifdef SERVER_LOGGING
        server_logging::LogExited(EXIT_FAILURE, error.what());
#else
        std::cerr << error.what() << std::endl;
#endif
        return EXIT_FAILURE;
    }
}
