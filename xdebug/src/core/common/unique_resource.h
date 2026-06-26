#pragma once

namespace xdebug_core {

template <typename T, typename Releaser>
class UniqueResource {
public:
    UniqueResource() : value_(T()), empty_(T()), owns_(false) {}
    explicit UniqueResource(T value) : value_(value), empty_(T()), owns_(value != T()) {}
    UniqueResource(T value, T empty) : value_(value), empty_(empty), owns_(value != empty) {}

    UniqueResource(const UniqueResource&) = delete;
    UniqueResource& operator=(const UniqueResource&) = delete;

    UniqueResource(UniqueResource&& other) noexcept
        : value_(other.release()), empty_(other.empty_), owns_(value_ != empty_) {}

    UniqueResource& operator=(UniqueResource&& other) noexcept {
        if (this != &other) {
            reset(other.release());
            empty_ = other.empty_;
        }
        return *this;
    }

    ~UniqueResource() { reset(); }

    T get() const { return value_; }
    bool valid() const { return owns_; }
    explicit operator bool() const { return owns_; }

    T release() {
        T old = value_;
        value_ = empty_;
        owns_ = false;
        return old;
    }

    void reset(T next = T()) {
        if (owns_) Releaser()(value_);
        value_ = next;
        owns_ = next != empty_;
    }

private:
    T value_;
    T empty_;
    bool owns_;
};

} // namespace xdebug_core
