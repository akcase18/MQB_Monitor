/**
 * @file signal_picker_screen.hpp
 * @brief Full-screen signal picker, opened from ConfiguratorScreen's Signal
 * row.
 */
#pragma once

#include <lvgl.h>

#include "theme.hpp"

namespace gui
{

    class SignalPickerScreen
    {
        public:
            using DoneCallback = void (*)(void *ctx, uint16_t signal_id);

            /**
             * @param signal_names Signal names, indexed by catalog position.
             * @param signal_ids   Parallel array of catalog signal ids.
             * @param num_signals  Length of both arrays.
             */
            void init(const Theme             &theme,
                      const char *const *const signal_names,
                      const uint16_t          *signal_ids,
                      uint16_t                 num_signals,
                      DoneCallback             done_cb,
                      void                    *ctx);

            /**
             * Opens the screen, pre-selecting `current_signal_id`; returns
             * to `return_screen` when closed.
             */
            void open(uint16_t current_signal_id, lv_obj_t *return_screen);

            lv_obj_t *screen() const
            {
                return screen_;
            }

        private:
            static void done_cb_handler(lv_event_t *e);

            lv_obj_t *screen_        = nullptr;
            lv_obj_t *roller_        = nullptr;
            lv_obj_t *return_screen_ = nullptr;

            const uint16_t *signal_ids_  = nullptr;
            uint16_t        num_signals_ = 0;

            DoneCallback done_cb_  = nullptr;
            void        *done_ctx_ = nullptr;
    };

} // namespace gui
