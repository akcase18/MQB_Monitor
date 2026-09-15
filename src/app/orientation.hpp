/**
 * @file orientation.hpp
 * @brief Translation between the three orientation enums.
 *
 * `core::LayoutOrientation` (persisted config), `gui::LayoutOrientation`
 * (the layout algorithm) and `DisplayOrientation` (platform.hpp) all say
 * the same thing and deliberately do not know about each other -- that
 * separation is what keeps `core/` free of `gui/` and both free of
 * `platform/`'s concrete types.
 *
 * Somebody has to translate, and it is the composition root's job, but it
 * is the same three functions for every product. Inline and header-only:
 * they compile to nothing.
 */
#pragma once

#include "config.hpp"
#include "layout.hpp"
#include "platform.hpp"

namespace app
{

    inline gui::LayoutOrientation to_gui_orientation(
        core::LayoutOrientation orientation)
    {
        return orientation == core::LayoutOrientation::Vertical
                   ? gui::LayoutOrientation::Vertical
                   : gui::LayoutOrientation::Horizontal;
    }

    inline core::LayoutOrientation to_core_orientation(
        gui::LayoutOrientation orientation)
    {
        return orientation == gui::LayoutOrientation::Vertical
                   ? core::LayoutOrientation::Vertical
                   : core::LayoutOrientation::Horizontal;
    }

    inline DisplayOrientation to_display_orientation(
        gui::LayoutOrientation orientation)
    {
        return orientation == gui::LayoutOrientation::Vertical
                   ? DisplayOrientation::Portrait
                   : DisplayOrientation::Landscape;
    }

} // namespace app
