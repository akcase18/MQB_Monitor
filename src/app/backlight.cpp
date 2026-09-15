#include "backlight.hpp"

#include "platform.hpp"

#include <lvgl.h>

namespace app
{
    namespace backlight
    {

        namespace
        {
            /**
             * ------------------------------------------------------------------
             * Backlight blanking
             *
             * Two moments need the panel dark while something ugly happens on
             * it:
             *
             *   * boot -- the panel's GRAM holds power-on garbage until a first
             *     full frame lands (TeensyDisplay::begin() holds the pin low
             * for exactly this);
             *   * an accent-color change -- which tears every screen down and
             *     builds them again (see rebuild_screens_for_theme_change), a
             *     white flash followed by SettingsScreen sliding back in.
             *
             * Each waits on its own signal, and they are deliberately different
             * signals, because what "settled" means differs by path.
             *
             * BOOT counts completed render passes. Instrumenting the simulator:
             * pass 1 is the full 480x320 initial paint, and the passes after it
             * repaint small regions as the readouts populate. Lighting up on
             * pass 1 shows the dashboard mid-populate. Counting passes is
             * immune to how long they take, which matters because the panel is
             * far slower than a dev host.
             *
             * A REBUILD waits for the display to go quiet instead. Its landing
             * screen (SettingsScreen) has no refresh task at all, so once drawn
             * it stops rendering entirely -- counting passes there would wait
             * for a pass that never comes.
             *
             * Note what does NOT work, since it looks like the obvious
             * unification: quiet-period detection on the boot path. It held
             * only while INFO_SCREEN_REFRESH_MS was 200, where the gap before
             * the readouts populated (~98ms) was shorter than the idle gaps
             * between steady-state redraws (131-198ms), leaving a window
             * between them. At 100ms that ordering inverts -- the populate gap
             * grows to ~106ms while idle gaps fall to 23-91ms -- and no
             * threshold exists: a low one fires on pass 1, a high one never
             * fires at all. Any future change to INFO_SCREEN_REFRESH_MS moves
             * these numbers again, which is why boot no longer depends on them.
             *
             * On hardware both paths also cover something LVGL cannot report:
             * the last flush's DMA is still in flight when flush() returns (see
             * TeensyDisplay::flush), so even a completed pass isn't fully on
             * the panel at the moment it is reported.
             *-----------------------------------------------------------------*/

            /**
             * Pass 1 is the initial full paint; the ones after it are the
             * readouts filling in. Three leaves a pass of margin over the two
             * that were actually needed when measured -- cheap, at one
             * INFO_SCREEN_REFRESH_MS each, and the failure it guards against
             * (lighting mid-populate) is the visible one. */
            constexpr uint8_t BOOT_SETTLE_PASSES = 3;

            /**
             * Rebuild path only, where the landing screen is static and this is
             * really just "has rendering stopped". Only needs to exceed the gap
             * between flushes *within* one pass (~17ms measured), so there is
             * generous margin; it is not tuned against the redraw cadence and
             * does not move with INFO_SCREEN_REFRESH_MS. */
            constexpr uint32_t BACKLIGHT_QUIET_MS = 100;

            /**
             * Backstop for both paths: light the panel regardless once this
             * long has passed since it went dark. A signal that never arrives
             * would otherwise leave the dashboard dark indefinitely -- the one
             * failure mode this mechanism must not have. */
            constexpr uint32_t BACKLIGHT_SETTLE_MAX_MS = 750;

            /* How often the watchdog above runs. */
            constexpr uint32_t BACKLIGHT_WATCH_POLL_MS = 25;

            enum class BacklightState
            {
                Live,               /* lit, at backlight_pct() */
                AwaitingBootPasses, /**
                                     * boot: off until BOOT_SETTLE_PASSES land
                                     */
                Blanked,            /* off, and the transition isn't over yet */
                Settling,           /* off -- waiting for rendering to stop */
            };

            BacklightState &backlight_state()
            {
                /**
                 * Boot starts here: the pin is already low out of
                 * TeensyDisplay::begin(), and counting render passes is what
                 * eventually lights it. */
                static BacklightState state
                    = BacklightState::AwaitingBootPasses;
                return state;
            }

            /** When display_flush_cb last ran, in clock millis -- the input to
             * the quiet test. Updated on every flush, not just a pass's last,
             * so a pass still in progress keeps pushing the deadline out. */
            uint32_t &last_flush_ms()
            {
                static uint32_t ms = 0;
                return ms;
            }

            /** Completed render passes seen since boot, capped by
             * BOOT_SETTLE_PASSES. */
            uint8_t &boot_passes()
            {
                static uint8_t passes = 0;
                return passes;
            }

            /** The repeating watchdog, live while the panel is dark. */
            lv_timer_t *&backlight_settle_timer()
            {
                static lv_timer_t *timer = nullptr;
                return timer;
            }

            /** When the current Settling wait began -- the input to the
             * backstop. */
            uint32_t &settle_started_ms()
            {
                static uint32_t ms = 0;
                return ms;
            }

            /** The brightness the panel returns to once it's lit again. */
            uint8_t &backlight_pct()
            {
                static uint8_t pct = 100;
                return pct;
            }

            /** Sets the brightness, now if the panel is lit and on the way back
             * up if it isn't -- so a slider move that lands mid-blank isn't
             * either lost or allowed to light the panel early. */
            void apply_backlight(uint8_t pct)
            {
                backlight_pct() = pct;
                if (backlight_state() == BacklightState::Live)
                {
                    platform_get().display.set_backlight(pct);
                }
            }

            /** The one place the panel is ever lit. Tears down the watchdog on
             * the way out -- deleting a timer from inside its own callback is
             * supported, lv_timer_handler() guards for exactly that. */
            void light_backlight()
            {
                backlight_state() = BacklightState::Live;
                platform_get().display.set_backlight(backlight_pct());

                if (backlight_settle_timer() != nullptr)
                {
                    lv_timer_t *timer        = backlight_settle_timer();
                    backlight_settle_timer() = nullptr;
                    lv_timer_delete(timer);
                }
            }

            /**
             * Runs while the panel is dark. Owns the rebuild path's quiet test
             * and the backstop for both paths; boot's pass counting lives in
             * display_flush_cb, where the passes actually arrive. */
            void backlight_watch_cb(lv_timer_t *timer)
            {
                (void)timer;
                if (backlight_state() == BacklightState::Live)
                {
                    return;
                }

                uint32_t now = platform_get().clock.millis();
                if (now - settle_started_ms() >= BACKLIGHT_SETTLE_MAX_MS)
                {
                    light_backlight(); /* signal never arrived -- see the
                                          backstop */
                    return;
                }

                if (backlight_state() == BacklightState::Settling
                    && now - last_flush_ms() >= BACKLIGHT_QUIET_MS)
                {
                    light_backlight();
                }
            }

            /** Starts (or restarts) the dark period: records when it began and
             * makes sure the watchdog is running.
             *
             * Safe to call from inside display_flush_cb even though that runs
             * within LVGL's own timer handler -- lv_timer_create() flags the
             * list as changed and the handler restarts its iteration
             * (lv_timer.c). */
            void begin_backlight_wait()
            {
                settle_started_ms() = platform_get().clock.millis();
                if (backlight_settle_timer() == nullptr)
                {
                    backlight_settle_timer() = lv_timer_create(
                        backlight_watch_cb, BACKLIGHT_WATCH_POLL_MS, nullptr);
                }
            }

            /** Kills the light until whatever is about to happen on screen has
             * finished. Something must walk the state back to Live afterwards,
             * or the panel stays dark -- see settings_screen_loaded_cb, the
             * only caller's counterpart. */
            void blank_backlight()
            {
                backlight_state() = BacklightState::Blanked;
                platform_get().display.set_backlight_off();
                begin_backlight_wait();
            }
        } // namespace

        void arm()
        {
            begin_backlight_wait();
        }

        void set_brightness(uint8_t pct)
        {
            apply_backlight(pct);
        }

        void blank_for_transition()
        {
            blank_backlight();
        }

        bool begin_settle_if_blanked()
        {
            if (backlight_state() != BacklightState::Blanked)
            {
                return false;
            }
            backlight_state() = BacklightState::Settling;
            return true;
        }

        void note_flush(bool completed_pass)
        {
            last_flush_ms() = platform_get().clock.millis();

            /**
             * The draw buffer covers only a fraction of the screen, so one
             * full frame takes several flushes; the caller passes
             * lv_display_flush_is_last() for the one that completes a
             * pass. Boot counts those -- see BacklightState above for why
             * it counts rather than timing them, and why the rebuild path
             * does not. */
            if (backlight_state() == BacklightState::AwaitingBootPasses
                && completed_pass)
            {
                if (++boot_passes() >= BOOT_SETTLE_PASSES)
                {
                    light_backlight();
                }
            }
        }

    } // namespace backlight
} // namespace app
