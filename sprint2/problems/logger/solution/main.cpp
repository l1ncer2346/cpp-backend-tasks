#include "my_logger.h"

int main() {
    Logger::GetInstance().SetTimestamp(std::chrono::system_clock::time_point{0s});
    LOG("Hello logger"sv);
}
