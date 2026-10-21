#pragma once

#include <string>

// What navigation does when preserving the bound project's draft fails.
//
// This header exists because the decision used to live only inside
// `MainWindow::NavigateAsync`, where a window is required to exercise it, and the
// behaviour it encoded was wrong: ANY failure of `storage_->SaveDraft` produced
// `co_return` before `ShowFolder(...)`, so navigation was abandoned. When the
// bound project's record no longer exists, `SaveDraft` matches on `id`, changes
// zero rows and throws on EVERY attempt, so the application could not navigate to
// any project again -- and, because `AppWindow().Closing` and the in-app Exit both
// route through the same coroutine, it could not be closed either. The only
// recovery was to kill the process.
//
// It is deliberately a pure function over a value: it performs no IO, holds no
// lock and reads no window state, so the decision can be executed deterministically
// in a native test without creating a window (AGENTS.md §3). It adds no service
// owner, record or state machine; the durable rules it reacts to are enforced
// inside Storage, which is where a direct caller is stopped.
namespace mayasaba
{
    // Why the draft could not be preserved.
    enum class DraftSaveFailure
    {
        None,
        // The bound project's record is gone. Retrying the same write cannot
        // succeed, so the binding itself is invalid rather than the attempt.
        ProjectRecordMissing,
        // The store refused the write for a reason that may not recur: a
        // quarantined connection, an exhausted free-space reserve, a closed
        // transaction. The binding may still be valid.
        StoreRefused,
        // Anything else. Treated like StoreRefused: the binding is not proven
        // invalid, so it is not silently discarded.
        Other
    };

    // What navigation does next.
    enum class NavigationDisposition
    {
        // The draft was preserved, or there was nothing to preserve.
        Proceed,
        // The binding is no longer valid. Clear it, treat the project as unbound,
        // and CONTINUE to the selector (or to exit) instead of abandoning the
        // navigation. Failing closed still applies to the draft -- it is never
        // reported as saved -- but the failure must not be terminal.
        UnbindAndProceed,
        // The binding is probably still valid, so it is preserved and the
        // navigation is refused for this attempt. This is retryable: nothing about
        // it prevents a later attempt from succeeding.
        StopAndNotify
    };

    inline NavigationDisposition DecideNavigationAfterDraftFailure(DraftSaveFailure const failure) noexcept
    {
        switch (failure)
        {
        case DraftSaveFailure::None:
            return NavigationDisposition::Proceed;
        case DraftSaveFailure::ProjectRecordMissing:
            // The one failure that cannot be retried into success. Refusing the
            // navigation would wedge every later navigation and the window close.
            return NavigationDisposition::UnbindAndProceed;
        case DraftSaveFailure::StoreRefused:
        case DraftSaveFailure::Other:
            // Refuse this attempt, keep the binding, keep the text. A later
            // attempt can still succeed, so this is not a wedge.
            return NavigationDisposition::StopAndNotify;
        }
        return NavigationDisposition::StopAndNotify;
    }

    inline char const* DraftSaveFailureText(DraftSaveFailure const failure) noexcept
    {
        switch (failure)
        {
        case DraftSaveFailure::None: return "none";
        case DraftSaveFailure::ProjectRecordMissing: return "the project record no longer exists";
        case DraftSaveFailure::StoreRefused: return "the local store refused the write";
        case DraftSaveFailure::Other: return "the local store reported an error";
        }
        return "an unclassified failure";
    }
}
