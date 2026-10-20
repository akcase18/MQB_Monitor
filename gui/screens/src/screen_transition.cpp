#include "screen_transition.hpp"
#include "mem_placement.hpp"

namespace gui
{

#if GUI_SNAPSHOT_TRANSITIONS
    namespace
    {
        /* One slide's state, owned by the transition screen and freed with
         * it (transition_deleted_cb). */
        struct Transition
        {
                lv_obj_t      *target;
                lv_draw_buf_t *snapshot;
        };

        void transition_deleted_cb(lv_event_t *e)
        {
            auto *t = static_cast<Transition *>(lv_event_get_user_data(e));
            lv_draw_buf_destroy(t->snapshot);
            lv_free(t);
        }

        void set_x_cb(void *img, int32_t v)
        {
            lv_obj_set_x(static_cast<lv_obj_t *>(img), v);
        }

        void set_y_cb(void *img, int32_t v)
        {
            lv_obj_set_y(static_cast<lv_obj_t *>(img), v);
        }

        void slide_done_cb(lv_anim_t *a)
        {
            auto     *img   = static_cast<lv_obj_t *>(a->var);
            lv_obj_t *trans = lv_obj_get_screen(img);
            auto     *t     = static_cast<Transition *>(
                lv_obj_get_user_data(trans));

            /* Something else loaded a screen mid-slide (a remote event,
             * say): that load wins, and this one just cleans up. */
            if (lv_screen_active() == trans)
            {
                lv_screen_load(t->target);
            }

            /* Not lv_obj_delete(): `img` is this animation's own var. */
            lv_obj_delete_async(trans);
        }
    } // namespace

    FLASHMEM void slide_screen_in(lv_obj_t             *screen,
                                  lv_screen_load_anim_t anim,
                                  uint32_t              time_ms)
    {
        const int32_t w = lv_display_get_horizontal_resolution(nullptr);
        const int32_t h = lv_display_get_vertical_resolution(nullptr);

        int32_t           from = 0;
        lv_anim_exec_xcb_t exec = nullptr;
        switch (anim)
        {
            case LV_SCR_LOAD_ANIM_OVER_BOTTOM:
                from = -h;
                exec = set_y_cb;
                break;
            case LV_SCR_LOAD_ANIM_OVER_TOP:
                from = h;
                exec = set_y_cb;
                break;
            case LV_SCR_LOAD_ANIM_OVER_RIGHT:
                from = -w;
                exec = set_x_cb;
                break;
            case LV_SCR_LOAD_ANIM_OVER_LEFT:
                from = w;
                exec = set_x_cb;
                break;
            default:
                lv_screen_load_anim(screen, anim, time_ms, 0, false);
                return;
        }

        lv_draw_buf_t *snapshot
            = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB565);
        auto *t = static_cast<Transition *>(lv_malloc(sizeof(Transition)));
        if (snapshot == nullptr || t == nullptr)
        {
            if (snapshot != nullptr)
            {
                lv_draw_buf_destroy(snapshot);
            }
            lv_free(t);
            lv_screen_load_anim(screen, anim, time_ms, 0, false);
            return;
        }
        t->target   = screen;
        t->snapshot = snapshot;

        lv_obj_t *trans = lv_obj_create(nullptr);
        lv_obj_remove_style_all(trans);
        lv_obj_set_size(trans, w, h);
        lv_obj_set_user_data(trans, t);
        lv_obj_add_event_cb(trans, transition_deleted_cb, LV_EVENT_DELETE, t);

        lv_obj_t *img = lv_image_create(trans);
        lv_obj_remove_style_all(img);
        lv_image_set_src(img, snapshot);
        exec(img, from);

        /* Load without redrawing: the panel still holds the outgoing
         * screen, and every area the slide invalidates from here on is
         * one the image covers. The layout pass has to happen in here
         * too -- left to the next refresh, sizing the screen and the
         * image invalidates the whole screen, and `trans` has no
         * background, so that frame comes out white. */
        lv_display_enable_invalidation(nullptr, false);
        lv_screen_load(trans);
        lv_obj_update_layout(trans);
        lv_display_enable_invalidation(nullptr, true);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, img);
        lv_anim_set_exec_cb(&a, exec);
        lv_anim_set_values(&a, from, 0);
        lv_anim_set_duration(&a, time_ms);
        lv_anim_set_completed_cb(&a, slide_done_cb);
        lv_anim_start(&a);
    }
#else
    FLASHMEM void slide_screen_in(lv_obj_t             *screen,
                                  lv_screen_load_anim_t anim,
                                  uint32_t              time_ms)
    {
        lv_screen_load_anim(screen, anim, time_ms, 0, false);
    }
#endif

} // namespace gui
