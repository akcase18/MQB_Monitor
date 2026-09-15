#include "entry_point.hpp"

/**
 * Off Arduino there is no framework main(), so this is it: the same
 * setup()-then-loop-forever shape the cores implement, against whichever
 * composition root got compiled into this build.
 *
 * UNIT_TEST is excluded because the test environments compile src/ too
 * (test_build_src = yes), and a Unity runner brings its own main().
 */
#if !defined(ARDUINO) && !defined(UNIT_TEST)

int main()
{
    setup();

    for (;;)
    {
        loop();
    }

    return 0;
}

#endif
