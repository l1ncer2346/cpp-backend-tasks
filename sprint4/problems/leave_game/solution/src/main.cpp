#include "sdk.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/program_options.hpp>

#include "application.h"
#include "http_server.h"
#include "json_loader.h"
#include "postgres.h"
#include "request_handler.h"
#include "server_logging.h"
#include "state_storage.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace net = boost::asio;
namespace sys = boost::system;

namespace {

constexpr const char kDbUrlEnvName[] = "GAME_DB_URL";

struct Options {
    std::filesystem::path config_file;
    std::filesystem::path www_root;
    std::optional<int> tick_period;
    bool randomize_spawn_points = false;
    std::optional<std::filesystem::path> state_file;
    std::optional<int> save_state_period;
};

Options ParseOptions(int argc, const char* argv[]) {
    namespace po = boost::program_options;
    po::options_description description("Allowed options");
    description.add_options()
        ("help,h", "produce help message")
        ("tick-period,t", po::value<int>()->value_name("milliseconds"), "set tick period")
        ("config-file,c", po::value<std::string>()->value_name("file")->required(), "set config file path")
        ("www-root,w", po::value<std::string>()->value_name("dir"), "set static files root")
        ("randomize-spawn-points", "spawn dogs at random positions")
        ("state-file", po::value<std::string>()->value_name("file"), "set game state file path")
        ("save-state-period", po::value<int>()->value_name("milliseconds"), "set game state save period");

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
    if (variables.contains("state-file")) {
        options.state_file = variables["state-file"].as<std::string>();
    }
    if (variables.contains("save-state-period")) {
        options.save_state_period = variables["save-state-period"].as<int>();
        if (*options.save_state_period <= 0) {
            throw std::invalid_argument("Save state period must be positive");
        }
    }
    return options;
}

std::string GetDbUrl() {
    if (const auto* url = std::getenv(kDbUrlEnvName)) {
        return url;
    }
    throw std::runtime_error(std::string(kDbUrlEnvName) + " environment variable not found");
}

class AutomaticTicker final : public std::enable_shared_from_this<AutomaticTicker> {
public:
    AutomaticTicker(net::io_context& ioc, app::Application& app, std::chrono::milliseconds period)
        : timer_(ioc)
        , app_(app)
        , period_(period) {
    }

    void Start() {
        timer_.expires_after(period_);
        timer_.async_wait([self = shared_from_this()](sys::error_code ec) {
            if (ec) {
                return;
            }
            try {
                self->app_.Tick(self->period_.count());
            } catch (const std::exception& error) {
                server_logging::LogError(0, error.what(), "tick");
            }
            self->Start();
        });
    }

    void Stop() {
        timer_.cancel();
    }

private:
    net::steady_timer timer_;
    app::Application& app_;
    std::chrono::milliseconds period_;
};

template <typename Function>
void RunWorkers(unsigned count, Function&& function) {
    count = std::max(1u, count);
    std::vector<std::jthread> workers;
    workers.reserve(count - 1);
    while (--count) {
        workers.emplace_back(function);
    }
    function();
}

}  // namespace

int main(int argc, const char* argv[]) {
    server_logging::Init();
    try {
        const Options options = ParseOptions(argc, argv);
        auto [game, extra] = json_loader::LoadGame(options.config_file);

        std::unique_ptr<app::StateSaver> state_saver;
        if (options.state_file) {
            if (auto snapshot = state_storage::Load(*options.state_file)) {
                game.Restore(*snapshot);
            }
            std::optional<std::chrono::milliseconds> save_period;
            if (options.save_state_period) {
                save_period = std::chrono::milliseconds{*options.save_state_period};
            }
            state_saver = std::make_unique<app::StateSaver>(game, *options.state_file, save_period);
        }

        const unsigned num_threads = std::max(1u, std::thread::hardware_concurrency());
        postgres::RecordsRepository records{num_threads, GetDbUrl()};
        app::Application application{game, records};
        application.SetListener(state_saver.get());

        net::io_context ioc(num_threads);
        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](const sys::error_code& ec, int) {
            if (!ec) {
                ioc.stop();
            }
        });

        http_handler::RequestHandler handler{application, extra, options.www_root, options.tick_period.has_value(),
                                             options.randomize_spawn_points};
        const auto address = net::ip::make_address("0.0.0.0");
        constexpr net::ip::port_type port = 8080;
        http_server::ServeHttp(ioc, {address, port}, [&handler](auto&& request, auto&& send, std::string address) {
            handler(std::forward<decltype(request)>(request), std::forward<decltype(send)>(send),
                    std::move(address));
        });

        std::shared_ptr<AutomaticTicker> ticker;
        if (options.tick_period) {
            ticker = std::make_shared<AutomaticTicker>(ioc, application, std::chrono::milliseconds(*options.tick_period));
            ticker->Start();
        }

        server_logging::LogStarted(port, "0.0.0.0");

        RunWorkers(num_threads, [&ioc] {
            ioc.run();
        });

        if (ticker) {
            ticker->Stop();
        }
        // сохраняем состояние при штатном завершении
        if (state_saver) {
            state_saver->Save();
        }
        server_logging::LogExited(EXIT_SUCCESS);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        server_logging::LogExited(EXIT_FAILURE, error.what());
        return EXIT_FAILURE;
    }
}
