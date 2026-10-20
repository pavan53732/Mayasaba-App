#pragma once
// Theme-following brushes for imperatively built surfaces.
//
// A `{ThemeResource}` reference written in XAML re-resolves itself when Windows
// switches between light and dark. A brush read out of the resource dictionary at
// build time does not: the element keeps the concrete Brush instance it was handed,
// so a card realized before the switch keeps the old palette. In dark mode that
// leaves dark ink on a dark panel, which is unreadable.
//
// Rebuilding those cards on every theme change would work, but it would discard the
// reader's focus, reading position and any open detail. Instead every imperative
// lookup returns a shared, mutable instance from this cache, and a live theme change
// copies the new colours onto those same instances. Elements already holding a
// cached brush therefore repaint in place: nothing is rebuilt and no state is lost.
//
// High contrast changes resource types (a gradient becomes a solid system brush) and
// drops decorative depth. A cached instance keeps its own type and is painted flat
// with the system colour, so it still follows the switch; the existing high-contrast
// path in MainWindow additionally removes the decoration outright.

#include <string>
#include <winrt/Microsoft.UI.Xaml.Media.h>

namespace mayasaba::ui
{
    // Returns the shared brush for `key`, creating it from the current theme on first
    // use. The returned instance stays valid across theme changes and is updated in
    // place, so callers may keep using the reference they already applied.
    winrt::Microsoft::UI::Xaml::Media::Brush Token(wchar_t const* key);

    // Re-resolves every cached brush from the current theme and repaints the existing
    // instances. Safe to call when nothing changed.
    void RefreshThemeBrushes();
}
