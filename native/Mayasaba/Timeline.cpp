#include "pch.h"
#include "Timeline.h"
#include "RichText.h"

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>

namespace mayasaba::ui
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Media;

    namespace
    {
        Brush Token(wchar_t const* key)
        {
            auto const value = Application::Current().Resources().TryLookup(box_value(key));
            if (auto const brush = value.try_as<Brush>())
            {
                return brush;
            }
            return SolidColorBrush{};
        }

        FontFamily BodyFont() { return FontFamily{L"Segoe UI Variable Text"}; }
        FontFamily MonoFont() { return FontFamily{L"Cascadia Mono"}; }
        using FontWeight = winrt::Windows::UI::Text::FontWeight;
        FontWeight SemiBold() { return winrt::Windows::UI::Text::FontWeights::SemiBold(); }

        TextBlock Label(winrt::hstring const& text, double size, Brush const& brush)
        {
            TextBlock block{};
            block.FontFamily(BodyFont());
            block.FontSize(size);
            block.TextWrapping(TextWrapping::Wrap);
            block.Foreground(brush);
            block.Text(text);
            return block;
        }

        std::string FormatBytes(uint64_t bytes)
        {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1);
            if (bytes < 1024ULL) ss << bytes << " B";
            else if (bytes < 1024ULL * 1024ULL) ss << (static_cast<double>(bytes) / 1024.0) << " KB";
            else if (bytes < 1024ULL * 1024ULL * 1024ULL) ss << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB";
            else ss << (static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0)) << " GB";
            return ss.str();
        }

        // A small semantic status dot. The caller always renders the matching
        // status text beside it, so colour is never the only carrier of meaning.
        Microsoft::UI::Xaml::Shapes::Ellipse Dot(Brush const& fill, double size)
        {
            Microsoft::UI::Xaml::Shapes::Ellipse dot{};
            dot.Width(size);
            dot.Height(size);
            dot.Fill(fill);
            dot.VerticalAlignment(VerticalAlignment::Center);
            return dot;
        }

        // The evidence spine: a narrow semantic edge that distinguishes controller
        // authority from conversation. Two nested Borders avoid any layout
        // dependency; the outer draws only the left edge.
        Border CardWithSpine(FrameworkElement const& content, Brush const& spine, double radius,
                             double padding)
        {
            Border surface{};
            surface.Background(Token(L"MayasabaPanelBrush"));
            surface.BorderBrush(Token(L"MayasabaBorderBrush"));
            surface.BorderThickness(Thickness{0, 1, 1, 1});
            surface.CornerRadius(CornerRadius{0, radius, radius, 0});
            surface.Padding(Thickness{padding, padding - 2, padding, padding - 2});
            surface.Child(content);

            Border card{};
            card.BorderBrush(spine);
            card.BorderThickness(Thickness{3, 0, 0, 0});
            card.CornerRadius(CornerRadius{radius, 0, 0, radius});
            card.Child(surface);
            return card;
        }

        // A footer holding the optional actions. Keeping actions on one row with a
        // consistent order means the reader always finds them in the same place.
        StackPanel ActionRow()
        {
            StackPanel row{};
            row.Orientation(Orientation::Horizontal);
            row.Spacing(8);
            return row;
        }

        Button QuietButton(winrt::hstring const& text, winrt::hstring const& automationName)
        {
            Button button{};
            button.Content(box_value(text));
            button.Padding(Thickness{12, 6, 12, 6});
            button.CornerRadius(CornerRadius{11});
            button.FontSize(12);
            button.FontFamily(BodyFont());
            Automation::AutomationProperties::SetName(button, automationName);
            return button;
        }

        // Wires a Show/Hide details toggle. `open` is the persisted initial state so a
        // card rebuilt to stay last in the timeline keeps the reader's expansion, and
        // every toggle is reported back so the caller can persist it.
        void WireDetails(Button const& toggle, StackPanel const& host, bool open,
                         hstring const& showName, hstring const& hideName,
                         mayasaba::ui::MotionController* motion,
                         std::function<void(std::string const&, bool)> const& report,
                         std::string const& identity)
        {
            if (open)
            {
                host.Visibility(Visibility::Visible);
                toggle.Content(box_value(hstring{L"Hide details"}));
                Automation::AutomationProperties::SetName(toggle, hideName);
            }
            auto weakHost = make_weak(host);
            auto weakToggle = make_weak(toggle);
            toggle.Click([motion, weakHost, weakToggle, showName, hideName, report, identity](auto const&, auto const&)
            {
                auto const detail = weakHost.get();
                auto const button = weakToggle.get();
                if (!detail || !button) return;
                bool const showing = detail.Visibility() == Visibility::Visible;
                if (showing)
                {
                    detail.Visibility(Visibility::Collapsed);
                    button.Content(box_value(hstring{L"Show details"}));
                    Automation::AutomationProperties::SetName(button, showName);
                }
                else
                {
                    detail.Visibility(Visibility::Visible);
                    if (motion) motion->Appear(detail);
                    button.Content(box_value(hstring{L"Hide details"}));
                    Automation::AutomationProperties::SetName(button, hideName);
                }
                if (report) report(identity, !showing);
            });
        }
    }

    Brush EdgeBrushFor(EntryState state)
    {
        switch (state)
        {
        case EntryState::Saved:
        case EntryState::Informational:
            return Token(L"MayasabaBrandBrush");
        case EntryState::Provisional:
        case EntryState::Waiting:
            return Token(L"MayasabaAmberBrush");
        case EntryState::Running:
            return Token(L"MayasabaBlueBrush");
        case EntryState::Recovered:
        case EntryState::Certified:
            return Token(L"MayasabaMintBrush");
        case EntryState::Blocked:
        case EntryState::Unknown:
        case EntryState::Failed:
            return Token(L"MayasabaDangerBrush");
        case EntryState::Paused:
        case EntryState::Stopped:
        default:
            return Token(L"MayasabaSoftBrush");
        }
    }

    hstring StateLabel(EntryState state)
    {
        switch (state)
        {
        case EntryState::Saved: return L"Saved";
        case EntryState::Provisional: return L"Provisional";
        case EntryState::Waiting: return L"Waiting";
        case EntryState::Running: return L"Running";
        case EntryState::Paused: return L"Paused";
        case EntryState::Stopped: return L"Stopped";
        case EntryState::Blocked: return L"Blocked";
        case EntryState::Unknown: return L"Unknown";
        case EntryState::Failed: return L"Failed";
        case EntryState::Recovered: return L"Recovered";
        case EntryState::Certified: return L"Certified";
        case EntryState::Informational:
        default: return L"Recorded";
        }
    }

    TimelineRenderer::TimelineRenderer(MotionController& motion) : motion_(motion)
    {
        items_ = winrt::single_threaded_observable_vector<IInspectable>();
    }

    TimelineRenderer::~TimelineRenderer()
    {
        // Release the observable source before the renderer's storage dies, so a
        // queued repeater callback cannot observe a half-destroyed model.
        if (items_) items_.Clear();
        entries_.clear();
        shown_.clear();
        tokens_.clear();
        tokenIndex_.clear();
    }

    void TimelineRenderer::ReplaceAll(std::vector<TimelineEntry> entries)
    {
        entries_ = std::move(entries);
        // Restored history is not new content: every entry is marked shown so no
        // entrance transition replays on restore.
        shown_.assign(entries_.size(), true);
        tokens_.clear();
        tokenIndex_.clear();
        tokens_.reserve(entries_.size());
        items_.Clear();
        for (std::size_t index = 0; index < entries_.size(); ++index)
        {
            auto const token = nextToken_++;
            tokens_.push_back(token);
            tokenIndex_[token] = index;
            items_.Append(box_value(token));
        }
    }

    void TimelineRenderer::Append(TimelineEntry entry)
    {
        auto const index = entries_.size();
        bool const silent = entry.silentInsert;
        entries_.push_back(std::move(entry));
        shown_.push_back(silent);
        auto const token = nextToken_++;
        tokens_.push_back(token);
        tokenIndex_[token] = index;
        items_.Append(box_value(token));
    }

    void TimelineRenderer::RemoveLast()
    {
        if (entries_.empty()) return;
        auto const token = tokens_.back();
        entries_.pop_back();
        shown_.pop_back();
        tokens_.pop_back();
        tokenIndex_.erase(token);
        if (items_.Size() > 0)
        {
            items_.RemoveAt(items_.Size() - 1);
        }
    }

    UIElement TimelineRenderer::GetElement(ElementFactoryGetArgs const& args)
    {
        auto const data = args.Data();
        if (!data) return nullptr;
        int64_t token = 0;
        try
        {
            token = unbox_value<int64_t>(data);
        }
        catch (...)
        {
            return nullptr;
        }
        auto const found = tokenIndex_.find(token);
        if (found == tokenIndex_.end()) return nullptr;
        auto const position = found->second;
        if (position >= entries_.size()) return nullptr;
        auto element = Build(entries_[position], position);
        // Insertion motion runs exactly once per entry, even after recycling.
        if (position < shown_.size() && !shown_[position])
        {
            shown_[position] = true;
            if (auto const framework = element.try_as<FrameworkElement>())
            {
                motion_.SlideIn(framework);
            }
        }
        return element;
    }

    void TimelineRenderer::RecycleElement(ElementFactoryRecycleArgs const& args)
    {
        // Recycling releases the visual. Nothing here retains the element, so no
        // animation callback or pooled container can hold a recycled item.
        if (auto const element = args.Element())
        {
            element.Opacity(1);
            if (auto const framework = element.try_as<FrameworkElement>())
            {
                if (auto const transform = framework.RenderTransform().try_as<CompositeTransform>())
                {
                    transform.TranslateX(0);
                    transform.TranslateY(0);
                }
            }
        }
    }

    UIElement TimelineRenderer::Build(TimelineEntry const& entry, std::size_t)
    {
        auto const ink = Token(L"MayasabaInkBrush");
        auto const muted = Token(L"MayasabaMutedBrush");
        auto const soft = Token(L"MayasabaSoftBrush");
        auto const edge = EdgeBrushFor(entry.state);

        RichTextOptions options{};
        options.measure = measure_;
        options.copy = copy_;
        options.allowCopy = static_cast<bool>(copy_);

        // ---- Test fixture identification ------------------------------------
        if (entry.kind == EntryKind::FixtureBanner)
        {
            StackPanel content{};
            content.Spacing(4);
            TextBlock heading = Label(entry.title.empty() ? L"TEST FIXTURE" : to_hstring(entry.title), 14, Token(L"MayasabaInkBrush"));
            heading.FontWeight(SemiBold());
            content.Children().Append(heading);
            content.Children().Append(Label(to_hstring(entry.body), 12.5, muted));
            auto banner = CardWithSpine(content, Token(L"MayasabaAmberBrush"), 14, 14);
            banner.Margin(Thickness{0, 0, 0, 4});
            Automation::AutomationProperties::SetName(banner,
                L"Test fixture notice. This content is not real agent work.");
            motion_.ApplyElevation(banner, 8);
            return banner;
        }

        // ---- User contribution ----------------------------------------------
        if (entry.kind == EntryKind::UserContribution)
        {
            StackPanel content{};
            content.Spacing(6);

            StackPanel meta{};
            meta.Orientation(Orientation::Horizontal);
            meta.Spacing(8);
            TextBlock author = Label(to_hstring(entry.author.empty() ? std::string{"You"} : entry.author), 12, muted);
            author.FontWeight(SemiBold());
            meta.Children().Append(author);
            meta.Children().Append(Dot(edge, 7));
            meta.Children().Append(Label(StateLabel(entry.state), 12, muted));
            if (!entry.timestamp.empty())
            {
                meta.Children().Append(Label(to_hstring(entry.timestamp), 12, soft));
            }
            content.Children().Append(meta);

            TextBlock body = Label(to_hstring(entry.body), 15.5, ink);
            body.IsTextSelectionEnabled(true);
            content.Children().Append(body);

            if (!entry.attachments.empty())
            {
                StackPanel attList{};
                attList.Spacing(6);
                attList.Margin(Thickness{0, 6, 0, 0});
                for (auto const& att : entry.attachments)
                {
                    Border chip{};
                    chip.Background(Token(L"MayasabaPanelBrush"));
                    chip.BorderBrush(Token(L"MayasabaBorderBrush"));
                    chip.BorderThickness(Thickness{1});
                    chip.CornerRadius(CornerRadius{8});
                    chip.Padding(Thickness{10, 6, 10, 6});

                    StackPanel chipContent{};
                    chipContent.Orientation(Orientation::Horizontal);
                    chipContent.Spacing(8);

                    chipContent.Children().Append(Dot(Token(L"MayasabaBrandBrush"), 7));

                    TextBlock nameBlock = Label(to_hstring(att.fileName), 12, ink);
                    nameBlock.FontWeight(SemiBold());
                    chipContent.Children().Append(nameBlock);

                    std::string sizeStr = FormatBytes(att.fileSize);
                    chipContent.Children().Append(Label(to_hstring(sizeStr), 11, muted));

                    if (!att.sha256.empty())
                    {
                        std::string shortDigest = "sha256:" + att.sha256.substr(0, 8) + "...";
                        TextBlock digestBlock = Label(to_hstring(shortDigest), 10.5, soft);
                        digestBlock.FontFamily(MonoFont());
                        chipContent.Children().Append(digestBlock);
                    }

                    chip.Child(chipContent);
                    Automation::AutomationProperties::SetName(chip,
                        to_hstring("Attachment: " + att.fileName + ", " + FormatBytes(att.fileSize)));
                    attList.Children().Append(chip);
                }
                content.Children().Append(attList);
            }

            Border bubble{};
            bubble.Background(Token(L"MayasabaBrandQuietBrush"));
            bubble.BorderBrush(Token(L"MayasabaBorderBrush"));
            bubble.BorderThickness(Thickness{1});
            bubble.CornerRadius(CornerRadius{14});
            bubble.Padding(Thickness{16, 12, 16, 12});
            bubble.MaxWidth(measure_ * 0.9);
            bubble.HorizontalAlignment(HorizontalAlignment::Right);
            bubble.Child(content);
            Automation::AutomationProperties::SetName(bubble,
                L"Your saved message. " + StateLabel(entry.state));
            motion_.ApplyElevation(bubble, 8);
            return bubble;
        }

        // ---- One consolidated agent reply -----------------------------------
        if (entry.kind == EntryKind::AgentReply)
        {
            StackPanel content{};
            content.Spacing(10);

            StackPanel header{};
            header.Orientation(Orientation::Horizontal);
            header.Spacing(9);
            header.Children().Append(Dot(edge, 9));
            TextBlock title = Label(to_hstring(entry.title.empty() ? std::string{"Mayasaba · one reviewed reply"} : entry.title), 14, ink);
            title.FontWeight(SemiBold());
            header.Children().Append(title);
            if (!entry.timestamp.empty())
            {
                header.Children().Append(Label(to_hstring(entry.timestamp), 12, soft));
            }
            content.Children().Append(header);

            content.Children().Append(RenderRichText(entry.body, options));

            if (!entry.attribution.empty())
            {
                TextBlock attribution = Label(to_hstring(entry.attribution), 12, muted);
                attribution.FontStyle(winrt::Windows::UI::Text::FontStyle::Italic);
                content.Children().Append(attribution);
            }

            // Show details reveals the three contributions, the recorded writer and
            // both peer reviews of the exact final revision. It is an inline
            // expansion, never a new route.
            if (entry.detailsAvailable && !entry.details.empty())
            {
                auto const actions = ActionRow();
                auto const toggle = QuietButton(L"Show details", L"Show reply details");

                StackPanel detailsHost{};
                detailsHost.Spacing(10);
                detailsHost.Visibility(Visibility::Collapsed);
                detailsHost.Margin(Thickness{0, 4, 0, 0});
                {
                    Microsoft::UI::Xaml::Shapes::Rectangle divider{};
                    divider.Height(1);
                    divider.Fill(Token(L"MayasabaLineBrush"));
                    detailsHost.Children().Append(divider);
                    detailsHost.Children().Append(RenderRichText(entry.details, options));
                }

                WireDetails(toggle, detailsHost, entry.detailsOpen,
                                L"Show reply details", L"Hide reply details", &motion_, detailsToggle_, entry.identity);
                actions.Children().Append(toggle);

                if (copy_)
                {
                    auto const copyReply = QuietButton(L"Copy reply", L"Copy the consolidated reply");
                    auto handler = copy_;
                    auto const plain = RichTextToPlainText(entry.body);
                    auto weakCopy = make_weak(copyReply);
                    copyReply.Click([handler, plain, weakCopy](auto const&, auto const&)
                    {
                        handler(hstring{plain});
                        if (auto const button = weakCopy.get())
                        {
                            button.Content(box_value(hstring{L"Copied"}));
                        }
                    });
                    actions.Children().Append(copyReply);
                }
                content.Children().Append(actions);
                content.Children().Append(detailsHost);
            }

            auto card = CardWithSpine(content, edge, 18, 16);
            card.MaxWidth(measure_ + 40);
            card.HorizontalAlignment(HorizontalAlignment::Left);
            Automation::AutomationProperties::SetName(card,
                L"Agent reply. " + StateLabel(entry.state) + L". Written by the recorded rotating writer and reviewed by both peers.");
            motion_.ApplyElevation(card, 8);
            return card;
        }

        // ---- Controller-owned card ------------------------------------------
        StackPanel card{};
        card.Spacing(8);

        StackPanel header{};
        header.Orientation(Orientation::Horizontal);
        header.Spacing(9);
        header.Children().Append(Dot(edge, 9));
        TextBlock title = Label(to_hstring(entry.title.empty() ? std::string{"Mayasaba"} : entry.title), 14, ink);
        title.FontWeight(SemiBold());
        header.Children().Append(title);
        header.Children().Append(Label(StateLabel(entry.state), 12, muted));
        if (!entry.timestamp.empty())
        {
            header.Children().Append(Label(to_hstring(entry.timestamp), 12, soft));
        }
        card.Children().Append(header);

        if (!entry.consequence.empty())
        {
            TextBlock consequence = Label(to_hstring(entry.consequence), 14, ink);
            consequence.IsTextSelectionEnabled(true);
            card.Children().Append(consequence);
        }
        if (!entry.body.empty())
        {
            card.Children().Append(Label(to_hstring(entry.body), 13, muted));
        }

        StackPanel provenance{};
        provenance.Spacing(2);
        if (!entry.operation.empty())
        {
            TextBlock operation = Label(to_hstring(entry.operation), 11.5, soft);
            operation.FontFamily(MonoFont());
            operation.IsTextSelectionEnabled(true);
            provenance.Children().Append(operation);
        }
        if (!entry.nextAction.empty())
        {
            provenance.Children().Append(Label(to_hstring(entry.nextAction), 12, muted));
        }
        if (provenance.Children().Size() > 0) card.Children().Append(provenance);

        if (entry.detailsAvailable && !entry.details.empty())
        {
            auto const actions = ActionRow();
            auto const toggle = QuietButton(L"Show details", L"Show card details");
            StackPanel detailsHost{};
            detailsHost.Spacing(10);
            detailsHost.Visibility(Visibility::Collapsed);
            {
                Microsoft::UI::Xaml::Shapes::Rectangle divider{};
                divider.Height(1);
                divider.Fill(Token(L"MayasabaLineBrush"));
                detailsHost.Children().Append(divider);
                detailsHost.Children().Append(RenderRichText(entry.details, options));
            }
            WireDetails(toggle, detailsHost, entry.detailsOpen,
                        L"Show card details", L"Hide card details", &motion_, detailsToggle_, entry.identity);
            actions.Children().Append(toggle);
            card.Children().Append(actions);
            card.Children().Append(detailsHost);
        }

        auto surface = CardWithSpine(card, edge, 18, 16);
        surface.MaxWidth(measure_ + 40);
        surface.HorizontalAlignment(HorizontalAlignment::Left);
        Automation::AutomationProperties::SetName(surface,
            L"Controller card. " + to_hstring(entry.title) + L". " + StateLabel(entry.state));
        motion_.ApplyElevation(surface, 8);
        return surface;
    }
}
