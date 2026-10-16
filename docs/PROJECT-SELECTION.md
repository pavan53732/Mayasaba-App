# Mayasaba — Screen 2 project selection

Derived implementation reference. The complete system description is authoritative.

## Project selection contract — approved 2026-10-15

This user-approved change supersedes the earlier direct-to-Chat launch rule. The selector is now implemented natively together with the Welcome and Chat screens; see [the validation record](../native/LOCAL-VALIDATION.md). The runtime acceptance that depends on the CLI adapters, session admission and three-agent reply orchestration remains pending and is not claimed.

### Launch and navigation

- Until the first successful project binding, launch shows Screen 1 Welcome. Next, Skip and Escape advance to Screen 2; Back returns to Welcome.
- After any project has been successfully bound, subsequent launches show Screen 2 Project selection, even when the last project is known. Welcome is not automatically repeated. Do not automatically bind the last project or open its Chat.
- Screen 2 is the existing selection presentation state, not a new dashboard, top-level workspace, service or state machine. Chat remains the only persistent work surface.
- File → Switch Project opens this same selector outside Chat content. Retain the old project and its unsent draft while selecting. Cancel or Escape returns to the old Chat unchanged; visiting the selector does not pause, stop, close or retarget work.
- On startup, Escape does not bypass selection into Chat. Back to Welcome exists only in the initial introduction flow; a later selector offers Exit, and a switch selector offers Cancel.
- Successful authorized selection opens Chat without a second Next action. Recent history restores docked; a project with no saved contributions opens with the centered composer. Fresh Start after application relaunch remains required for each project's CLI sessions.

### Recent projects

Display known projects from Project-service records through read-only queries, including name, canonical folder path, last successfully opened time, and explicit open/closed condition. Sort by last successful open descending, with stable project identity as the tie-breaker. An empty list explains that no projects have been opened yet. Do not discover projects by scanning the disk.

Show currently working/paused/blocked activity only from current controller observations, separately from open/closed condition. Last-opened time, unfinished history, an installed CLI or a previous run is not proof of running activity. When current activity is unknown, label it unknown. Do not infer completion from inactivity or closure.

Clicking a row revalidates folder identity, availability, permissions and existing safety guards before binding. A missing, moved, inaccessible or identity-mismatched root cannot open automatically. Offer Locate folder with an exact explanation. A different folder must never inherit another project's identity or history; if safe identity continuity cannot be established, keep the original record intact and allow the different root only as a separate project. A missing folder is never silently recreated.

Closed projects remain listed. Selecting one opens retained history read-only; it does not authorize Reopen Project or Start. Do not expose a closure toggle as a shortcut around the existing lifecycle contract.

### Folder actions

- **Open existing folder:** open the native Windows directory picker, then validate and bind the chosen existing root. Empty existing folders are valid. Cancellation or rejection preserves the selection view and drafts.
- **Create new project folder:** obtain an explicit parent location and folder name, show the final path, and create only that requested empty directory after authorized validation. This is a distinct filesystem effect, not an implicit effect of Open existing folder. Refuse overwrite/name collision, invalid names, unavailable parents and unsafe/reparse escapes. If creation has an unknown result, reconcile the same intent; do not blindly retry. If creation succeeds but binding fails, disclose the created path and keep it intact rather than silently deleting it. No Git initialization, template generation, dependency installation, scan or CLI launch follows creation.
- Root creation must use the existing Policy/Workspace/Execution boundaries and Project-service binding; declare its typed command and recovery evidence before native enablement. No new service owner or state machine is authorized by this UI change.

### Drafts, state and safety

Project drafts remain attached to their original project. Opening the selector never copies the active project's draft or attachments into another project. A separately entered pre-binding draft remains an unsent draft; selection must not overwrite an existing target-project draft. Preserve both and ask for explicit disposition if there is a collision. No selection submits a message.

Changing the active binding still requires the existing isolation, pause and bounded-reconciliation rules. Missing proof keeps the candidate unbound and the old project authoritative; restore the old Chat with the precise blocker. Selection never silently stops active work, merges histories, retargets sessions, closes or reopens a project. Background supervision and immediate control handling remain active during selection.

The list, selection and drafts never depend on CLI readiness. Successful binding refreshes bounded non-mutating detection of all three CLIs but launches no working session. Screen 3 retains no folder control inside Chat. Start/Stop and retained same-run project Start permissions keep their existing semantics.

### Acceptance additions

Verify first-run Welcome → selector → Chat; later launch → selector without automatic last-project binding; recent-project ordering and truthful status; empty list; open/closed history restoration; missing/moved/identity-mismatched roots; picker cancellation; create success/collision/denial/unknown outcome; switch cancellation and unsafe-switch refusal; separate project drafts and pre-binding draft collision; keyboard focus, screen-reader labels, reduced motion and high contrast. No result is certified by the HTML preview.
