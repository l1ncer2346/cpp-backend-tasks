#pragma once

#include <compare>
#include <functional>
#include <utility>

namespace util {

template <typename Value, typename Tag>
class Tagged {
public:
    using ValueType = Value;
    using TagType = Tag;

    explicit Tagged(Value&& value)
        : value_(std::move(value)) {
    }

    explicit Tagged(const Value& value)
        : value_(value) {
    }

    const Value& operator*() const noexcept {
        return value_;
    }

    Value& operator*() noexcept {
        return value_;
    }

    auto operator<=>(const Tagged&) const = default;

private:
    Value value_;
};

template <typename TaggedValue>
struct TaggedHasher {
    size_t operator()(const TaggedValue& value) const {
        return std::hash<typename TaggedValue::ValueType>{}(*value);
    }
};

}  // namespace util
