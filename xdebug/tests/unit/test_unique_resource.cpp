#include "common/unique_resource.h"

#include <cassert>
#include <utility>

namespace {

struct FakeReleaser {
    void operator()(int value) const {
        ++release_count;
        last_released = value;
    }

    static int release_count;
    static int last_released;
};

int FakeReleaser::release_count = 0;
int FakeReleaser::last_released = 0;

typedef xdebug_core::UniqueResource<int, FakeReleaser> FakeResource;

void reset_counters() {
    FakeReleaser::release_count = 0;
    FakeReleaser::last_released = 0;
}

void test_destructor_releases_once() {
    reset_counters();
    {
        FakeResource resource(42, -1);
        assert(resource.valid());
        assert(resource.get() == 42);
    }
    assert(FakeReleaser::release_count == 1);
    assert(FakeReleaser::last_released == 42);
}

void test_move_transfers_ownership() {
    reset_counters();
    {
        FakeResource first(7, -1);
        FakeResource second(std::move(first));
        assert(!first.valid());
        assert(second.valid());
        assert(second.get() == 7);
    }
    assert(FakeReleaser::release_count == 1);
    assert(FakeReleaser::last_released == 7);
}

void test_reset_releases_previous_value() {
    reset_counters();
    {
        FakeResource resource(1, -1);
        resource.reset(2);
        assert(resource.valid());
        assert(resource.get() == 2);
        assert(FakeReleaser::release_count == 1);
        assert(FakeReleaser::last_released == 1);
    }
    assert(FakeReleaser::release_count == 2);
    assert(FakeReleaser::last_released == 2);
}

void test_release_disarms_destructor() {
    reset_counters();
    {
        FakeResource resource(9, -1);
        int raw = resource.release();
        assert(raw == 9);
        assert(!resource.valid());
    }
    assert(FakeReleaser::release_count == 0);
}

void test_empty_value_is_not_released() {
    reset_counters();
    {
        FakeResource resource(-1, -1);
        assert(!resource.valid());
    }
    assert(FakeReleaser::release_count == 0);
}

} // namespace

int main() {
    test_destructor_releases_once();
    test_move_transfers_ownership();
    test_reset_releases_previous_value();
    test_release_disarms_destructor();
    test_empty_value_is_not_released();
    return 0;
}
