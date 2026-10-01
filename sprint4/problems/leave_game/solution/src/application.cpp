#include "application.h"

#include "state_storage.h"

namespace app {

void Application::Tick(std::int64_t delta_ms) {
    const auto retired = game_.Tick(delta_ms);
    records_.Save(retired);
    if (listener_) {
        listener_->OnTick(std::chrono::milliseconds{delta_ms});
    }
}

void StateSaver::OnTick(std::chrono::milliseconds delta) {
    if (!period_) {
        return;
    }
    std::lock_guard lock{mutex_};
    since_last_save_ += delta;
    if (since_last_save_ >= *period_) {
        state_storage::Save(state_file_, game_.MakeSnapshot());
        since_last_save_ = std::chrono::milliseconds{0};
    }
}

void StateSaver::Save() {
    std::lock_guard lock{mutex_};
    state_storage::Save(state_file_, game_.MakeSnapshot());
}

}  // namespace app
