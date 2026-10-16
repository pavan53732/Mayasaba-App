#pragma once
// Bounded, safe rendering of agent reply prose into native WinUI blocks.
//
// The renderer understands a deliberately small Markdown subset (headings,
// paragraphs, lists, quotes, fenced code and horizontal rules) and never
// executes, evaluates or re-dispatches any rendered content. Rendered text is
// presentation only: it is never controller authority, never a command and
// never a completion claim.

#include <functional>
#include <string>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace mayasaba::ui
{
    struct RichTextOptions
    {
        // Reading measure for prose. Evidence and code blocks are allowed wider.
        double measure{720.0};
        // Invoked when the reader activates a code block's Copy control. When this
        // is empty, or allowCopy is false, no Copy control is emitted at all.
        std::function<void(winrt::hstring const&)> copy;
        bool allowCopy{true};
    };

    // Builds the block stack for one reply. Returns an empty stack for empty input.
    winrt::Microsoft::UI::Xaml::Controls::StackPanel RenderRichText(
        std::string const& text, RichTextOptions const& options);

    // Plain-text projection used for accessible names and for the Copy action of a
    // whole reply. Strips the Markdown markers the renderer recognises.
    std::wstring RichTextToPlainText(std::string const& text);
}
