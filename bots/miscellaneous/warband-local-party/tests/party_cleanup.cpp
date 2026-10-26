#include "party_cleanup.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <string>

struct ReleaseFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct RestoreFailure : std::logic_error {
    using std::logic_error::logic_error;
};

int main() {
    for (bool release_fails : {false, true}) {
        for (bool restore_fails : {false, true}) {
            int released = 0, restored = 0;
            bool caught_release = false, caught_restore = false;
            try {
                mb::party::finish_release([&] {
                    ++released;
                    assert(restored == 0);
                    if (release_fails) throw ReleaseFailure("original release failure");
                }, [&] {
                    ++restored;
                    assert(released == 1);
                    if (restore_fails) throw RestoreFailure("original restore failure");
                });
                assert(!release_fails && !restore_fails);
            } catch (const ReleaseFailure& e) {
                caught_release = true;
                assert(std::string(e.what()) == "original release failure");
            } catch (const RestoreFailure& e) {
                caught_restore = true;
                assert(std::string(e.what()) == "original restore failure");
            }
            assert(released == 1 && restored == 1);
            assert(caught_release == release_fails);
            assert(caught_restore == (!release_fails && restore_fails));
        }
    }
    // Preserve non-standard thrown objects too; cleanup must not replace them
    // with a generic standard exception or the later restoration failure.
    int second_calls = 0;
    try {
        mb::party::finish_release([] { throw 47; }, [&] {
            ++second_calls;
            throw RestoreFailure("later failure");
        });
        assert(false);
    } catch (int original) {
        assert(original == 47 && second_calls == 1);
    }
    std::puts("party cleanup: independent attempts and original first exception preserved");
}
