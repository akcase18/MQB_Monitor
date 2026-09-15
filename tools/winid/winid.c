/**
 * @file winid.c
 * @brief Prints the CoreGraphics window IDs owned by a process, so a
 * window can be screenshotted with `screencapture -l` instead of by
 * screen region.
 *
 * macOS only, and only useful for capturing the desktop simulator
 * (.pio/build/simulator/program) during GUI work.
 *
 * ## Why this exists
 *
 * `screencapture -R x,y,w,h` captures whatever is composited at those
 * coordinates **on the Space that is currently visible** -- not the window
 * you asked System Events for. If the simulator is on a background Space
 * (running a long build on one desktop while working on another, say, or
 * behind any app in fullscreen), System Events still reports a perfectly
 * plausible position and size, and the capture silently returns some other
 * application's pixels. It does not fail; it returns the wrong image. That
 * has happened three times on this project across two sessions.
 *
 * `screencapture -l <CGWindowID>` addresses the window itself: correct
 * regardless of Space, and it neither raises nor focuses anything, so it
 * cannot disturb whatever is in front. macOS ships no CLI for obtaining
 * that id and the system python3 has no Quartz bindings, which is the only
 * reason this file is here -- CGWindowListCopyWindowInfo is the whole
 * program.
 *
 * ## Build and use
 *
 *     clang -o winid tools/winid/winid.c \
 *           -framework CoreGraphics -framework CoreFoundation
 *
 *     PID=$(pgrep -f ".pio/build/simulator/program")
 *     WID=$(./winid "$PID" | head -1 | cut -d' ' -f1)
 *     screencapture -x -o -l"$WID" shot.png
 *
 * Output is one line per *titled* window, largest first:
 * `<windowid> <w> <h> <title>`.
 *
 * Two filtering decisions, both learned the hard way:
 *
 * **kCGWindowListOptionAll, not kCGWindowListOptionOnScreenOnly.** "On
 * screen" means *on the Space that is currently visible*, so filtering by
 * it reproduces the exact blind spot this tool exists to fix -- a
 * simulator on a background Space is missing from the listing even though
 * `screencapture -l` captures it perfectly well. The first version of this
 * file used OnScreenOnly and appeared to work, because the window happened
 * to be on the visible Space; it returned exit 3 the first time it was
 * pointed at one that was not.
 *
 * **Windows with a title, not simply the biggest.** An SDL process owns
 * several untitled sibling windows -- shadow and chrome helpers, and a
 * 500x500 one that is larger than the panel itself on the MQB product.
 * Picking the largest window therefore captures a blank square. Only the
 * real window carries a kCGWindowName, so that is the filter, with area
 * left as a tiebreak. (Reading window titles is part of what the Screen
 * Recording permission below covers; without it every name comes back
 * empty and this finds nothing, which fails loudly rather than capturing
 * the wrong thing.)
 *
 * Exit codes: 0 found, 1 the window list could not be read, 2 bad usage,
 * 3 no window for that pid. A 3 usually means the process died -- or, on
 * this project, that `pio test` has overwritten
 * `.pio/build/simulator/program` with the Unity test runner, which exits
 * immediately instead of opening a window. Re-run `pio run -e simulator`
 * before launching for a screenshot.
 *
 * Requires macOS Screen Recording permission for the terminal/editor doing
 * the capture; without it `screencapture` fails outright rather than
 * capturing the wrong thing.
 */
#include <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: winid <pid>\n");
        return 2;
    }
    int want = atoi(argv[1]);

    CFArrayRef list = CGWindowListCopyWindowInfo(
        kCGWindowListOptionAll | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID);
    if (list == NULL)
    {
        fprintf(stderr, "CGWindowListCopyWindowInfo failed\n");
        return 1;
    }

    struct Win
    {
        int  id;
        int  w;
        int  h;
        char name[256];
    } wins[64];

    int found = 0;
    for (CFIndex i = 0; i < CFArrayGetCount(list) && found < 64; i++)
    {
        CFDictionaryRef entry = CFArrayGetValueAtIndex(list, i);

        int         pid    = 0;
        CFNumberRef pidRef = CFDictionaryGetValue(entry, kCGWindowOwnerPID);
        if (pidRef == NULL
            || !CFNumberGetValue(pidRef, kCFNumberIntType, &pid)
            || pid != want)
        {
            continue;
        }

        int         wid   = 0;
        CFNumberRef idRef = CFDictionaryGetValue(entry, kCGWindowNumber);
        if (idRef == NULL
            || !CFNumberGetValue(idRef, kCFNumberIntType, &wid))
        {
            continue;
        }

        int             w      = 0;
        int             h      = 0;
        CFDictionaryRef bounds = CFDictionaryGetValue(entry,
                                                      kCGWindowBounds);
        CGRect          rect;
        if (bounds != NULL
            && CGRectMakeWithDictionaryRepresentation(bounds, &rect))
        {
            w = (int)rect.size.width;
            h = (int)rect.size.height;
        }

        /**
         * kCGWindowName, not kCGWindowOwnerName: the owner is the
         * process ("program") and every sibling window shares it, while
         * the title belongs to the one real window. */
        char        name[256] = "";
        CFStringRef nameRef   = CFDictionaryGetValue(entry, kCGWindowName);
        if (nameRef != NULL)
        {
            CFStringGetCString(
                nameRef, name, sizeof(name), kCFStringEncodingUTF8);
        }
        if (name[0] == '\0')
        {
            continue;
        }

        wins[found].id = wid;
        wins[found].w  = w;
        wins[found].h  = h;
        snprintf(wins[found].name, sizeof(wins[found].name), "%s", name);
        found++;
    }

    CFRelease(list);

    /**
     * Largest first. An SDL window brings small sibling windows along
     * under the same pid; the one to capture is the one with pixels in
     * it. Insertion sort -- `found` is a handful at most. */
    for (int i = 1; i < found; i++)
    {
        struct Win key = wins[i];
        int        j   = i - 1;
        while (j >= 0 && (long)wins[j].w * wins[j].h < (long)key.w * key.h)
        {
            wins[j + 1] = wins[j];
            j--;
        }
        wins[j + 1] = key;
    }

    for (int i = 0; i < found; i++)
    {
        printf("%d %d %d %s\n", wins[i].id, wins[i].w, wins[i].h,
               wins[i].name);
    }

    return found ? 0 : 3;
}
