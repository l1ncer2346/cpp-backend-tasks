#pragma once
#ifdef _WIN32
#include <sdkddkver.h>
#endif

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <chrono>
#include <memory>

#include "hotdog.h"
#include "result.h"

namespace net = boost::asio;
using namespace std::chrono_literals;

// Функция-обработчик операции приготовления хот-дога
using HotDogHandler = std::function<void(Result<HotDog> hot_dog)>;

// Класс "Кафетерий". Готовит хот-доги
class Cafeteria {
public:
    explicit Cafeteria(net::io_context& io)
        : io_{io} {
    }

    // Асинхронно готовит хот-дог и вызывает handler, как только хот-дог будет готов.
    // Этот метод может быть вызван из произвольного потока
    void OrderHotDog(HotDogHandler handler) {
        // Доступ к store_ и next_order_id_ сериализуем через strand_, так как OrderHotDog
        // может вызываться из разных потоков параллельно
        net::dispatch(strand_, [this, handler = std::move(handler)]() mutable {
            const int id = ++next_order_id_;
            auto order = std::make_shared<Order>(id, store_.GetSausage(), store_.GetBread(),
                                                  std::move(handler));
            RoastSausage(order);
            BakeBread(order);
        });
    }

private:
    // Продолжительности приготовления зафиксированы и лежат внутри допустимых для HotDog
    // интервалов
    static constexpr auto kSausageCookDuration = 1550ms;
    static constexpr auto kBreadCookDuration = 1100ms;

    // Общее состояние заказа. Живёт, пока не приготовятся оба ингредиента
    struct Order {
        Order(int id, std::shared_ptr<Sausage> sausage, std::shared_ptr<Bread> bread,
              HotDogHandler handler)
            : id{id}
            , sausage{std::move(sausage)}
            , bread{std::move(bread)}
            , handler{std::move(handler)} {
        }

        int id;
        std::shared_ptr<Sausage> sausage;
        std::shared_ptr<Bread> bread;
        HotDogHandler handler;
        int num_ready = 0;
        bool completed = false;
    };

    void RoastSausage(const std::shared_ptr<Order>& order) {
        auto sausage = order->sausage;
        sausage->StartFry(*gas_cooker_, [this, order, sausage] {
            auto timer = std::make_shared<net::steady_timer>(io_, kSausageCookDuration);
            timer->async_wait([this, order, sausage, timer](sys::error_code ec) {
                if (!ec) {
                    sausage->StopFry();
                }
                IngredientReady(order, ec);
            });
        });
    }

    void BakeBread(const std::shared_ptr<Order>& order) {
        auto bread = order->bread;
        bread->StartBake(*gas_cooker_, [this, order, bread] {
            auto timer = std::make_shared<net::steady_timer>(io_, kBreadCookDuration);
            timer->async_wait([this, order, bread, timer](sys::error_code ec) {
                if (!ec) {
                    bread->StopBaking();
                }
                IngredientReady(order, ec);
            });
        });
    }

    // Вызывается, когда один из ингредиентов заказа готов (или завершился с ошибкой).
    // Как только готовы оба ингредиента - собирает хот-дог и вызывает handler заказа
    void IngredientReady(const std::shared_ptr<Order>& order, sys::error_code ec) {
        net::dispatch(strand_, [order, ec] {
            if (order->completed) {
                return;
            }
            if (ec) {
                order->completed = true;
                order->handler(Result<HotDog>{
                    std::make_exception_ptr(std::runtime_error("Cooking failed: " + ec.message()))});
                return;
            }
            if (++order->num_ready == 2) {
                order->completed = true;
                try {
                    order->handler(Result<HotDog>{HotDog{order->id, order->sausage, order->bread}});
                } catch (...) {
                    order->handler(Result<HotDog>::FromCurrentException());
                }
            }
        });
    }

    net::io_context& io_;
    net::strand<net::io_context::executor_type> strand_{net::make_strand(io_)};
    // Используется для создания ингредиентов хот-дога
    Store store_;
    // Газовая плита. По условию задачи в кафетерии есть только одна газовая плита на 8 горелок
    // Используйте её для приготовления ингредиентов хот-дога.
    // Плита создаётся с помощью make_shared, так как GasCooker унаследован от
    // enable_shared_from_this.
    std::shared_ptr<GasCooker> gas_cooker_ = std::make_shared<GasCooker>(io_);
    int next_order_id_ = 0;
};
