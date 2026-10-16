#include "pch.h"
#include "RichText.h"

#include <winrt/Microsoft.UI.Xaml.Automation.h>

#include <cctype>
#include <string>
#include <vector>

namespace mayasaba::ui
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Documents;
    using namespace Microsoft::UI::Xaml::Media;
    using namespace Microsoft::UI::Xaml::Shapes;

    namespace
    {
        // Hard bounds: a reply renderer must not be able to build an unbounded
        // visual tree from hostile or runaway generated text.
        constexpr std::size_t MaxInputBytes = 262144;
        constexpr std::size_t MaxBlocks = 4000;

        FontFamily BodyFont() { return FontFamily{L"Segoe UI Variable Text"}; }
        FontFamily MonoFont() { return FontFamily{L"Cascadia Mono"}; }

        Brush Token(wchar_t const* key)
        {
            auto const value = Application::Current().Resources().TryLookup(box_value(key));
            if (auto const brush = value.try_as<Brush>())
            {
                return brush;
            }
            return SolidColorBrush{};
        }

        std::string Trim(std::string const& value)
        {
            std::size_t first = 0;
            std::size_t last = value.size();
            while (first < last && (value[first] == ' ' || value[first] == '\t' || value[first] == '\r')) ++first;
            while (last > first && (value[last - 1] == ' ' || value[last - 1] == '\t' || value[last - 1] == '\r')) --last;
            return value.substr(first, last - first);
        }

        bool StartsWith(std::string const& value, char const* prefix)
        {
            std::size_t const length = std::strlen(prefix);
            return value.size() >= length && value.compare(0, length, prefix) == 0;
        }

        // Ordered-list detection: "1. ", "12) " and similar.
        bool IsOrderedItem(std::string const& line, std::string& marker, std::string& rest)
        {
            std::size_t i = 0;
            while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
            if (i == 0 || i > 3 || i + 1 >= line.size()) return false;
            if (line[i] != '.' && line[i] != ')') return false;
            if (line[i + 1] != ' ') return false;
            marker = line.substr(0, i + 1);
            rest = Trim(line.substr(i + 2));
            return true;
        }

        // Inline formatting: **bold** and `code`. Anything else is literal text.
        void AppendInlines(TextBlock const& target, std::string const& text)
        {
            std::size_t i = 0;
            std::string pending;
            auto flush = [&]()
            {
                if (pending.empty()) return;
                Run run{};
                run.Text(to_hstring(pending));
                target.Inlines().Append(run);
                pending.clear();
            };
            while (i < text.size())
            {
                if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '*')
                {
                    std::size_t const close = text.find("**", i + 2);
                    if (close != std::string::npos && close > i + 2)
                    {
                        flush();
                        Bold bold{};
                        Run run{};
                        run.Text(to_hstring(text.substr(i + 2, close - i - 2)));
                        bold.Inlines().Append(run);
                        target.Inlines().Append(bold);
                        i = close + 2;
                        continue;
                    }
                }
                if (text[i] == '`')
                {
                    std::size_t const close = text.find('`', i + 1);
                    if (close != std::string::npos && close > i + 1)
                    {
                        flush();
                        // An inline technical identity. It carries no authority, is
                        // never executed, and is rendered as a distinct span rather
                        // than a hosted element: WinUI 3 does not support hosting a
                        // UIElement inside a TextBlock's inline flow, and a hosted
                        // element there terminates the process.
                        Span span{};
                        Run code{};
                        code.Text(to_hstring(text.substr(i + 1, close - i - 1)));
                        span.Inlines().Append(code);
                        span.Foreground(Token(L"MayasabaBrandStrongBrush"));
                        target.Inlines().Append(span);
                        i = close + 1;
                        continue;
                    }
                }
                pending.push_back(text[i]);
                ++i;
            }
            flush();
        }

        TextBlock Paragraph(std::string const& text, double size, bool muted, bool italic)
        {
            TextBlock block{};
            block.FontFamily(BodyFont());
            block.FontSize(size);
            block.TextWrapping(TextWrapping::Wrap);
            block.IsTextSelectionEnabled(true);
            block.Foreground(muted ? Token(L"MayasabaMutedBrush") : Token(L"MayasabaInkBrush"));
            if (italic) block.FontStyle(winrt::Windows::UI::Text::FontStyle::Italic);
            AppendInlines(block, text);
            return block;
        }

        // The evidence spine: a narrow semantic edge that separates controller
        // authority from conversation. Reused for quotes so the reading language
        // stays consistent.
        Border QuoteSurface(std::string const& text, double measure)
        {
            TextBlock body = Paragraph(text, 14.5, true, true);
            body.MaxWidth(measure);

            Border surface{};
            surface.Background(Token(L"MayasabaSecondaryBrush"));
            surface.BorderBrush(Token(L"MayasabaBorderBrush"));
            surface.BorderThickness(Thickness{0, 1, 1, 1});
            surface.CornerRadius(CornerRadius{0, 11, 11, 0});
            surface.Padding(Thickness{14, 10, 14, 10});
            surface.Child(body);

            Border edge{};
            edge.BorderBrush(Token(L"MayasabaSoftBrush"));
            edge.BorderThickness(Thickness{3, 0, 0, 0});
            edge.CornerRadius(CornerRadius{11, 0, 0, 11});
            edge.Child(surface);
            return edge;
        }
    }

    StackPanel RenderRichText(std::string const& text, RichTextOptions const& options)
    {
        StackPanel stack{};
        stack.Spacing(10);

        if (text.empty() || text.size() > MaxInputBytes)
        {
            if (!text.empty())
            {
                TextBlock notice{};
                notice.FontFamily(BodyFont());
                notice.FontSize(13);
                notice.TextWrapping(TextWrapping::Wrap);
                notice.Foreground(Token(L"MayasabaAmberBrush"));
                notice.Text(L"This reply exceeded the readable bound and was not rendered. Its recorded length is shown instead.");
                stack.Children().Append(notice);
                TextBlock size{};
                size.FontFamily(MonoFont());
                size.FontSize(12);
                size.Foreground(Token(L"MayasabaMutedBrush"));
                size.Text(to_hstring(std::to_string(text.size()) + " bytes recorded"));
                stack.Children().Append(size);
            }
            return stack;
        }

        std::vector<std::string> lines;
        {
            std::string current;
            for (char const c : text)
            {
                if (c == '\n')
                {
                    lines.push_back(current);
                    current.clear();
                }
                else
                {
                    current.push_back(c);
                }
            }
            lines.push_back(current);
        }

        std::size_t blocks = 0;
        std::size_t i = 0;
        while (i < lines.size() && blocks < MaxBlocks)
        {
            std::string const raw = lines[i];
            std::string const line = Trim(raw);

            if (line.empty())
            {
                ++i;
                continue;
            }

            // Fenced code block.
            if (StartsWith(line, "```"))
            {
                std::vector<std::string> body;
                ++i;
                while (i < lines.size() && !StartsWith(Trim(lines[i]), "```"))
                {
                    body.push_back(lines[i]);
                    ++i;
                }
                if (i < lines.size()) ++i; // consume the closing fence
                std::string code;
                for (std::size_t n = 0; n < body.size(); ++n)
                {
                    if (n) code.push_back('\n');
                    code += body[n];
                }

                TextBlock codeBlock{};
                codeBlock.FontFamily(MonoFont());
                codeBlock.FontSize(12.5);
                codeBlock.TextWrapping(TextWrapping::Wrap);
                codeBlock.IsTextSelectionEnabled(true);
                codeBlock.Foreground(Token(L"MayasabaInkBrush"));
                codeBlock.Text(to_hstring(code));

                StackPanel inner{};
                inner.Spacing(8);
                inner.Children().Append(codeBlock);

                if (options.allowCopy && options.copy && !code.empty())
                {
                    Button copy{};
                    copy.Content(box_value(hstring{L"Copy code"}));
                    copy.Padding(Thickness{12, 6, 12, 6});
                    copy.CornerRadius(CornerRadius{11});
                    copy.HorizontalAlignment(HorizontalAlignment::Left);
                    copy.FontSize(12);
                    Automation::AutomationProperties::SetName(copy, L"Copy code block");
                    auto handler = options.copy;
                    auto weakButton = make_weak(copy);
                    copy.Click([handler, code, weakButton](auto const&, auto const&)
                    {
                        handler(to_hstring(code));
                        if (auto const button = weakButton.get())
                        {
                            button.Content(box_value(hstring{L"Copied"}));
                        }
                    });
                    inner.Children().Append(copy);
                }

                Border surface{};
                surface.Background(Token(L"MayasabaCodeSurfaceBrush"));
                surface.BorderBrush(Token(L"MayasabaBorderBrush"));
                surface.BorderThickness(Thickness{1});
                surface.CornerRadius(CornerRadius{11});
                surface.Padding(Thickness{14, 12, 14, 12});
                surface.Child(inner);
                stack.Children().Append(surface);
                ++blocks;
                continue;
            }

            // Heading.
            if (line[0] == '#')
            {
                std::size_t level = 0;
                while (level < line.size() && line[level] == '#' && level < 6) ++level;
                if (level < line.size() && line[level] == ' ')
                {
                    std::string const heading = Trim(line.substr(level + 1));
                    double const size = level <= 1 ? 20.0 : (level == 2 ? 17.0 : 15.5);
                    TextBlock block = Paragraph(heading, size, false, false);
                    block.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
                    block.Margin(Thickness{0, 6, 0, 0});
                    block.MaxWidth(options.measure);
                    stack.Children().Append(block);
                    ++blocks;
                    ++i;
                    continue;
                }
            }

            // Horizontal rule.
            if (line == "---" || line == "***" || line == "___")
            {
                Microsoft::UI::Xaml::Shapes::Rectangle rule{};
                rule.Height(1);
                rule.Fill(Token(L"MayasabaLineBrush"));
                rule.MaxWidth(options.measure);
                rule.HorizontalAlignment(HorizontalAlignment::Left);
                stack.Children().Append(rule);
                ++blocks;
                ++i;
                continue;
            }

            // Blockquote.
            if (StartsWith(line, "> "))
            {
                std::string quoted = Trim(line.substr(2));
                stack.Children().Append(QuoteSurface(quoted, options.measure));
                ++blocks;
                ++i;
                continue;
            }

            // Unordered list.
            if (StartsWith(line, "- ") || StartsWith(line, "* ") || StartsWith(line, "+ "))
            {
                std::string const item = Trim(line.substr(2));
                Grid row{};
                row.ColumnSpacing(10);
                ColumnDefinition bulletColumn{};
                bulletColumn.Width(GridLengthHelper::FromValueAndType(18, GridUnitType::Pixel));
                row.ColumnDefinitions().Append(bulletColumn);
                ColumnDefinition textColumn{};
                textColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
                row.ColumnDefinitions().Append(textColumn);

                TextBlock bullet{};
                bullet.FontFamily(BodyFont());
                bullet.FontSize(15);
                bullet.Text(L"\u2022");
                bullet.Foreground(Token(L"MayasabaBrandBrush"));
                bullet.HorizontalAlignment(HorizontalAlignment::Right);
                row.Children().Append(bullet);

                TextBlock body = Paragraph(item, 15.5, false, false);
                Grid::SetColumn(body, 1);
                body.MaxWidth(options.measure);
                row.Children().Append(body);

                stack.Children().Append(row);
                ++blocks;
                ++i;
                continue;
            }

            // Ordered list.
            {
                std::string marker;
                std::string rest;
                if (IsOrderedItem(line, marker, rest))
                {
                    Grid row{};
                    row.ColumnSpacing(10);
                    ColumnDefinition markerColumn{};
                    markerColumn.Width(GridLengthHelper::FromValueAndType(26, GridUnitType::Pixel));
                    row.ColumnDefinitions().Append(markerColumn);
                    ColumnDefinition textColumn{};
                    textColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
                    row.ColumnDefinitions().Append(textColumn);

                    TextBlock label{};
                    label.FontFamily(BodyFont());
                    label.FontSize(15);
                    label.Text(to_hstring(marker));
                    label.Foreground(Token(L"MayasabaBrandBrush"));
                    label.HorizontalAlignment(HorizontalAlignment::Right);
                    row.Children().Append(label);

                    TextBlock body = Paragraph(rest, 15.5, false, false);
                    Grid::SetColumn(body, 1);
                    body.MaxWidth(options.measure);
                    row.Children().Append(body);

                    stack.Children().Append(row);
                    ++blocks;
                    ++i;
                    continue;
                }
            }

            // Plain paragraph: consecutive non-blank lines join into one block.
            std::string paragraph = line;
            ++i;
            while (i < lines.size())
            {
                std::string const next = Trim(lines[i]);
                if (next.empty() || StartsWith(next, "```") || StartsWith(next, "#") ||
                    StartsWith(next, "> ") || StartsWith(next, "- ") || StartsWith(next, "* ") ||
                    StartsWith(next, "+ ") || next == "---" || next == "***" || next == "___")
                {
                    break;
                }
                paragraph.push_back(' ');
                paragraph += next;
                ++i;
            }
            TextBlock body = Paragraph(paragraph, 15.5, false, false);
            body.MaxWidth(options.measure);
            stack.Children().Append(body);
            ++blocks;
        }

        if (blocks >= MaxBlocks)
        {
            TextBlock notice{};
            notice.FontFamily(BodyFont());
            notice.FontSize(12.5);
            notice.TextWrapping(TextWrapping::Wrap);
            notice.Foreground(Token(L"MayasabaAmberBrush"));
            notice.Text(L"Only the first readable blocks of this reply are shown; the complete record is unchanged.");
            stack.Children().Append(notice);
        }
        return stack;
    }

    std::wstring RichTextToPlainText(std::string const& text)
    {
        if (text.empty()) return {};
        std::string out;
        out.reserve(text.size());
        bool inFence = false;
        std::size_t i = 0;
        bool atLineStart = true;
        while (i < text.size())
        {
            if (atLineStart && StartsWith(text.substr(i, 3), "```"))
            {
                inFence = !inFence;
                i += 3;
                atLineStart = false;
                continue;
            }
            char const c = text[i];
            if (c == '\n')
            {
                atLineStart = true;
                out.push_back(' ');
                ++i;
                continue;
            }
            if (atLineStart && !inFence)
            {
                if (c == '#')
                {
                    ++i;
                    continue;
                }
                if ((c == '-' || c == '*' || c == '+') && i + 1 < text.size() && text[i + 1] == ' ')
                {
                    i += 2;
                    atLineStart = false;
                    continue;
                }
                if (c == '>' && i + 1 < text.size() && text[i + 1] == ' ')
                {
                    i += 2;
                    atLineStart = false;
                    continue;
                }
                atLineStart = false;
            }
            if (!inFence && c == '*' && i + 1 < text.size() && text[i + 1] == '*')
            {
                i += 2;
                continue;
            }
            if (c == '`')
            {
                ++i;
                continue;
            }
            out.push_back(c);
            ++i;
        }
        return to_hstring(Trim(out)).c_str();
    }
}
