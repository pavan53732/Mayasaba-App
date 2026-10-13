#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include <microsoft.ui.xaml.window.h>
#include <shobjidl_core.h>
#include <winrt/Windows.System.h>
#include <cwctype>
#include <limits>

namespace winrt::Mayasaba::implementation
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Media;
    using namespace Windows::Storage::Pickers;

    namespace
    {
        std::string Utf8(std::wstring const& s) { return winrt::to_string(winrt::hstring{s}); }
        winrt::hstring Utf16(std::string const& s) { return winrt::to_hstring(s); }
        void Visible(FrameworkElement const& element, bool show)
        {
            element.Visibility(show ? Visibility::Visible : Visibility::Collapsed);
        }
        std::string SubmissionId()
        {
            GUID value{};
            winrt::check_hresult(CoCreateGuid(&value));
            wchar_t buffer[40]{};
            StringFromGUID2(value, buffer, 40);
            return Utf8(buffer);
        }
    }

    MainWindow::MainWindow()
    {
        InitializeComponent();
        Title(L"Mayasaba · Engineering Control Room");
        draftTimer_ = DispatcherTimer{};
        draftTimer_.Interval(std::chrono::milliseconds(450));
        draftTimer_.Tick([this](IInspectable const&, IInspectable const&)
        {
            draftTimer_.Stop();
            PersistDraft();
        });
        try
        {
            storage_ = std::make_unique<mayasaba::Storage>(mayasaba::Storage::DefaultFile());
            if (auto key = storage_->LastBoundProject())
            {
                if (storage_->Project(*key))
                {
                    activeProject_ = *key;
                    ShowChat();
                    return;
                }
            }
        }
        catch (std::exception const& e)
        {
            storage_.reset();
            ShowNotice(L"SQLite startup failed. Folder binding and Send are blocked: " + Utf16(e.what()), true);
        }
        ShowWelcome();
    }

    void MainWindow::ShowNotice(hstring const& message, bool error)
    {
        Notice().Title(error ? L"Action blocked" : L"Mayasaba");
        Notice().Message(message);
        Notice().Severity(error ? InfoBarSeverity::Error : InfoBarSeverity::Informational);
        Notice().IsOpen(true);
    }

    void MainWindow::ShowWelcome()
    {
        Visible(WelcomeScreen(), true);
        Visible(FolderScreen(), false);
        Visible(ChatScreen(), false);
        Visible(NativeFileMenu(), false);
        NextButton().Focus(FocusState::Programmatic);
    }
    void MainWindow::ShowFolder()
    {
        Visible(WelcomeScreen(), false);
        Visible(FolderScreen(), true);
        Visible(ChatScreen(), false);
        Visible(NativeFileMenu(), false);
        PrebindDraftBox().Text(hstring{prebindingDraft_});
        ChooseFolderButton().Focus(FocusState::Programmatic);
    }
    void MainWindow::ShowChat()
    {
        if (!storage_ || activeProject_.empty()) { ShowFolder(); return; }
        auto project = storage_->Project(activeProject_);
        if (!project) { activeProject_.clear(); ShowFolder(); return; }
        draftTimer_.Stop();
        Visible(WelcomeScreen(), false);
        Visible(FolderScreen(), false);
        Visible(ChatScreen(), true);
        Visible(NativeFileMenu(), true);
        ProjectHeading().Text(Utf16(project->displayName));
        ProjectPathLabel().Text(Utf16(project->canonicalPath));
        ProjectCondition().Text(project->closed ?
            L"CLOSED · Explicit Reopen Project required" : L"Bound · No working CLI session admitted");
        ReopenButton().Visibility(project->closed ? Visibility::Visible : Visibility::Collapsed);
        StartButton().IsEnabled(!project->closed);
        PauseButton().IsEnabled(!project->closed);
        StopButton().IsEnabled(!project->closed);
        suppressDraft_ = true;
        ComposerBox().Text(Utf16(project->draft));
        suppressDraft_ = false;
        ComposerBox().IsReadOnly(project->closed);
        SendButton().IsEnabled(!project->closed && !project->draft.empty());
        RenderHistory();
        ComposerBox().Focus(FocusState::Programmatic);
    }

    void MainWindow::RenderHistory()
    {
        HistoryList().Children().Clear();
        auto messages = storage_->Contributions(activeProject_);
        bool populated = !messages.empty();
        Visible(EmptyChatPanel(), !populated);
        Visible(HistoryScroller(), populated);
        Grid::SetRow(ComposerPanel(), populated ? 2 : 1);
        ComposerPanel().VerticalAlignment(populated ? VerticalAlignment::Stretch : VerticalAlignment::Center);
        ComposerPanel().Margin(populated ? Thickness{12, 8, 12, 12} : Thickness{18, 135, 18, 0});
        ComposerPanel().Width(populated ? std::numeric_limits<double>::quiet_NaN() : 720.0);
        for (auto const& item : messages)
        {
            Border bubble{};
            bubble.CornerRadius(CornerRadius{14});
            bubble.Padding(Thickness{16, 12, 16, 12});
            bubble.MaxWidth(660);
            bubble.HorizontalAlignment(HorizontalAlignment::Right);
            StackPanel user{};
            user.Spacing(6);
            TextBlock label{};
            label.Text(L"You · Saved in SQLite");
            label.FontSize(12);
            TextBlock content{};
            content.Text(Utf16(item.body));
            content.TextWrapping(TextWrapping::Wrap);
            content.IsTextSelectionEnabled(true);
            user.Children().Append(label);
            user.Children().Append(content);
            bubble.Child(user);
            HistoryList().Children().Append(bubble);

            Border blocked{};
            blocked.CornerRadius(CornerRadius{13});
            blocked.Padding(Thickness{15, 12, 15, 12});
            blocked.BorderThickness(Thickness{1});
            blocked.BorderBrush(Application::Current().Resources().Lookup(
                winrt::box_value(L"MayasabaBorderBrush")).as<Brush>());
            StackPanel info{};
            info.Spacing(6);
            TextBlock status{};
            status.Text(L"Mayasaba · Waiting for verified CLI integration");
            TextBlock reason{};
            reason.FontSize(12);
            reason.TextWrapping(TextWrapping::Wrap);
            reason.Text(L"Saved only. No three-CLI reply, review, work authorization, or completion is claimed.");
            info.Children().Append(status);
            info.Children().Append(reason);
            blocked.Child(info);
            HistoryList().Children().Append(blocked);
        }
    }

    void MainWindow::PersistDraft()
    {
        if (suppressDraft_ || activeProject_.empty() || !storage_) return;
        try { storage_->SaveDraft(activeProject_, Utf8(ComposerBox().Text().c_str())); }
        catch (std::exception const& e)
        { ShowNotice(L"Draft not confirmed saved: " + Utf16(e.what()), true); }
    }

    void MainWindow::OnWelcomeNext(IInspectable const&, RoutedEventArgs const&) { ShowFolder(); }
    void MainWindow::OnWelcomeBack(IInspectable const&, RoutedEventArgs const&)
    {
        prebindingDraft_ = PrebindDraftBox().Text().c_str();
        ShowWelcome();
    }
    void MainWindow::OnPrebindDraftChanged(IInspectable const&, TextChangedEventArgs const&)
    { prebindingDraft_ = PrebindDraftBox().Text().c_str(); }
    void MainWindow::OnDraftChanged(IInspectable const&, TextChangedEventArgs const&)
    {
        if (suppressDraft_) return;
        bool open = false;
        if (storage_ && !activeProject_.empty())
        {
            try
            {
                auto project = storage_->Project(activeProject_);
                open = project && !project->closed;
            }
            catch (...) { open = false; }
        }
        SendButton().IsEnabled(open && !ComposerBox().Text().empty());
        if (open) { draftTimer_.Stop(); draftTimer_.Start(); }
    }
    void MainWindow::OnComposerKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e)
    {
        if (e.Key() == Windows::System::VirtualKey::Enter &&
            GetKeyState(VK_SHIFT) >= 0 && GetKeyState(VK_CONTROL) >= 0)
        {
            e.Handled(true);
            SendCurrent();
        }
    }
    void MainWindow::OnSend(IInspectable const&, RoutedEventArgs const&) { SendCurrent(); }
    void MainWindow::SendCurrent()
    {
        if (busy_ || !storage_ || activeProject_.empty()) return;
        std::wstring text = ComposerBox().Text().c_str();
        if (text.empty()) return;
        auto project = storage_->Project(activeProject_);
        if (!project || project->closed)
        {
            ShowNotice(L"Project closed. Explicitly reopen it before Send.", true);
            return;
        }
        std::wstring command = text;
        auto begin = command.find_first_not_of(L" \t\r\n");
        auto end = command.find_last_not_of(L" \t\r\n");
        if (begin != std::wstring::npos) command = command.substr(begin, end-begin+1);
        for (auto& c : command) c = static_cast<wchar_t>(towlower(c));
        if (command == L"/start" || command == L"/pause" || command == L"/resume" || command == L"/stop")
        {
            ShowNotice(L"Control recognized; no native CLI execution is admitted until adapters and confinement are proven.", true);
            return;
        }
        busy_ = true;
        draftTimer_.Stop();
        try
        {
            PersistDraft();
            // Persist one ID and exact payload before attempting owner commit.
            std::string submission = storage_->StageSubmission(activeProject_, SubmissionId(), Utf8(text));
            auto result = storage_->SaveContribution(activeProject_, submission, Utf8(text));
            if (result == mayasaba::SaveDisposition::Saved ||
                result == mayasaba::SaveDisposition::Duplicate)
            {
                suppressDraft_ = true;
                ComposerBox().Text(L"");
                suppressDraft_ = false;
                SendButton().IsEnabled(false);
                RenderHistory();
                ShowNotice(L"Message durably saved. CLI interpretation/replies are not yet implemented.");
            }
            else
                ShowNotice(result == mayasaba::SaveDisposition::Unknown ?
                    L"Commit result UNKNOWN. Draft retained; do not retry until original submission reconciles." :
                    L"Submission rejected without changing the draft.", true);
        }
        catch (std::exception const& e)
        { ShowNotice(L"Submission not confirmed: " + Utf16(e.what()), true); }
        busy_ = false;
    }

    winrt::fire_and_forget MainWindow::PickFolderAsync()
    {
        auto keepAlive = get_strong();
        try
        {
            PersistDraft();
            FolderPicker picker;
            picker.FileTypeFilter().Append(L"*");
            Microsoft::UI::Xaml::Window window = *this;
            HWND hwnd{};
            winrt::check_hresult(window.as<IWindowNative>()->get_WindowHandle(&hwnd));
            winrt::check_hresult(picker.as<IInitializeWithWindow>()->Initialize(hwnd));
            auto selected = co_await picker.PickSingleFolderAsync();
            if (!selected) co_return;
            if (!storage_) { ShowNotice(L"Store unavailable; project cannot be bound.", true); co_return; }
            auto root = mayasaba::ValidateProjectRoot(selected.Path().c_str());
            auto id = Utf8(root.stableIdentity);
            storage_->BindProject(id, Utf8(root.displayName), Utf8(root.canonicalPath));
            if (!prebindingDraft_.empty())
            {
                if (auto existing = storage_->Project(id); existing && existing->draft.empty())
                    storage_->SaveDraft(id, Utf8(prebindingDraft_));
                prebindingDraft_.clear();
            }
            // No CLI sessions can exist in this native slice; later builds must check
            // independent isolation/cessation evidence BEFORE modifying activeProject_.
            activeProject_ = id;
            ShowChat();
            ShowNotice(L"Project bound. No CLI was started and no repository scan occurred.");
        }
        catch (winrt::hresult_error const& e) { ShowNotice(L"Picker failed: " + e.message(), true); }
        catch (std::exception const& e) { ShowNotice(L"Project root refused: " + Utf16(e.what()), true); }
    }
    void MainWindow::OnChooseFolder(IInspectable const&, RoutedEventArgs const&) { PickFolderAsync(); }
    void MainWindow::OnSwitchProject(IInspectable const&, RoutedEventArgs const&) { PickFolderAsync(); }

    void MainWindow::OnStart(IInspectable const&, RoutedEventArgs const&)
    {
        ShowNotice(L"START BLOCKED: three native CLI adapters, independent applied context and pre-effect confinement proof are not implemented. No processes launched.", true);
    }
    void MainWindow::OnPause(IInspectable const&, RoutedEventArgs const&)
    { ShowNotice(L"No working CLI sessions were started. No external Pause is claimed."); }
    void MainWindow::OnStop(IInspectable const&, RoutedEventArgs const&)
    { ShowNotice(L"No working CLI sessions were started; no external effects are claimed ceased."); }
    void MainWindow::OnDiagnostics(IInspectable const&, RoutedEventArgs const&)
    {
        ShowNotice(L"Hermes, Kilo Code, Claude Code: UNSUPPORTED. Executable identity and native probes not implemented; no provider calls.", true);
    }
    void MainWindow::OnCloseProject(IInspectable const&, RoutedEventArgs const&)
    {
        if (!storage_ || activeProject_.empty()) return;
        try
        {
            PersistDraft();
            auto project = storage_->Project(activeProject_);
            if (!project || !storage_->SetClosure(activeProject_, project->version, true))
                throw std::runtime_error("Stale project version");
            ShowChat();
            ShowNotice(L"Project closed by explicit action; retained history remains visible.");
        }
        catch (std::exception const& e) { ShowNotice(L"Closure refused: " + Utf16(e.what()), true); }
    }
    void MainWindow::OnReopen(IInspectable const&, RoutedEventArgs const&)
    {
        if (!storage_ || activeProject_.empty()) return;
        try
        {
            auto project = storage_->Project(activeProject_);
            if (!project || !storage_->SetClosure(activeProject_, project->version, false))
                throw std::runtime_error("Stale project version");
            ShowChat();
            ShowNotice(L"Project reopened. CLI Start was not authorized.");
        }
        catch (std::exception const& e) { ShowNotice(L"Reopen refused: " + Utf16(e.what()), true); }
    }
}
