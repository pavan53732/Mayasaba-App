# C01 — Chat contribution, immediate control and turn admission

**Documentation status: NORMATIVE TRACE CONTRACT.** This pack restates the approved [Mayasaba authoritative description](../Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md): Continuous Chat, three-agent reply, immediate controls and persistence gate. [C06](./C06-BINDING-PRODUCT-DECISIONS.md) fixes exact control recognition and attachment bounds; [C07](./C07-VERSIONED-CONTRACT-REGISTRY.md) fixes the shared representation; [C08](./C08-TWELVE-STATE-MACHINES.md) and [C09](./C09-SELECTION-TRANSACTION-RECOVERY.md) fix the transition and transaction rules. It adds no project decision, service or state machine.

## Existing owners and identities

- **Message/Contribution** owns immutable UserContribution, turn identity, writer ordinal, consolidated reply and local draft links. **Project** owns binding and closure. **Work-request authority** owns independently scoped material actions. **Orchestrator/Gateway/MCF/Context** coordinate actual authorized three-agent delivery, sessions and current application proof. **SQLite Storage** commits owned state, event and outbound record atomically. **Policy and execution owners** govern effective physical effects. The **WinUI UI** only submits typed commands and projects observed results.
- Every enabled Send has one stable submission identity, complete text and selected attachment set; at most one immutable confirmed contribution may result. A valid immediate Pause/Stop has a **separate stable control-operation identity**. A confirmed reply-eligible contribution may allocate exactly one zero-based **project-local turn ordinal**. A control-only submission allocates neither turn nor writer ordinal.
- C07–C09 fix the shared JSON types, identifiers, errors, idempotency construction and owner commit boundary. Each enabled command still supplies its bounded command-specific payload schema and implementation mapping under C07.

## Send admission table

| Case | Initial fact | Only approved controller meaning | Forbidden |
| --- | --- | --- | --- |
| C01-01 | No bound authorized project root | Welcome and Project selection have no composer, attachment entry or Send control; message entry begins only in bound-project Chat (user confirmation, 2026-10-17) | Saving a contribution or silently transferring/deleting a legacy unbound draft |
| C01-02 | Project root bound and open; zero CLIs or no Start | Send attempts one contribution persist, agent processing waits | CLI readiness gating Send or auto-Start |
| C01-03 | Bound project CLOSED | Readable history; explicit authorized Reopen Project before new Send | Reopening by root selection or Start |
| C01-04 | Selected attachment still preparing | Retain same submission identity/draft through bounded whole-set byte verification | Silent omission or partial persist |
| C01-05 | Attachment source changes/fails or deadline expires | No incomplete contribution; preserve draft and exact blocker | Claim included/verified attachment |
| C01-06 | Exact full contribution independently confirmed durable | Display one saved contribution; only then admit *eligible* interpretation/turn processing | Treat persistence as work authorization |
| C01-07 | Definite contribution persistence failure | Clearly not saved; preserve draft if possible; no turn/ordinal/CLI dispatch | Fake saved, answer or approval |
| C01-08 | Lost ACK or crash leaves persist UNKNOWN | Reconcile same submission identity and durable owner facts before retry | Blind duplicate insertion or dispatch |
| C01-09 | Confirmed normal turn; Start valid; all three current | Three independent responses, selected writer, both exact-final-draft peer reviews, then one reply | One/two-agent fallback or unreviewed reply |
| C01-10 | Saved normal turn, Stop hold or missing Start | Truthful wait/block; no CLI launch from Send | Treat saved message as session permission |
| C01-11 | Late reply with wrong project, epoch, snapshot or turn | Keep historical attribution, reject as current completion | Cross-project/stale reply acceptance |
| C01-12 | Saved request for code change | Separate source-linked WorkRequest/FULL/user/policy gates apply | Authorization from a Chat answer alone |

A confirmed stored contribution, conversational interpretation, authorized work request, observed physical effect, and acceptance certification are **different proofs**.

## Immediate Pause/Stop and mixed intent

1. Execute a **deterministically recognized, already authorized** immediate control through its existing owner **without waiting** for contribution persistence, three agents, council or normal reply.
2. Record or attempt to record control provenance and report the actually **observed** result; a Stop receipt is not proof that descendant tools/services ceased effects.
3. For “Stop, then build X,” the **whole mixed draft** separately attempts one UserContribution under its **original submission identity**. The control's independent identity and effect cannot authorize X.
4. If that contribution commit is failed or uncertain, **no** non-control CLI notification, turn/writer ordinal, interpretation, work request or task may arise. Reconcile under the same submission ID before trying again. Never blindly repeat the already-observed Stop.
5. Only after confirmed persistence may X enter ordinary interpretation and scope/approval gates, while Stop remains effective; the message must not silently restart CLI sessions.

| Case | Control observation | Contribution result | Mandatory disposition |
| --- | --- | --- | --- |
| C01-13 | Authorized Stop acted | Mixed request confirmed | Stop once; X routed independently, not automatically authorized |
| C01-14 | Authorized Stop acted | Mixed request failed persist | Preserve Stop result; no X turn/ordinal/dispatch |
| C01-15 | Authorized Stop acted | Mixed request persist UNKNOWN | Reconcile same Send identity; never repeat Stop blindly |
| C01-16 | Authorized Stop acted | Control-only persist confirmed | No ChatTurn and no writer rotation advance |
| C01-17 | Natural-language control target/condition ambiguous | Any persist result | Clarify; do not infer destructive authority |
| C01-18 | Stop acknowledged but effect-capable helper still unproved ceased | Any | Preserve physically UNKNOWN outcome; block unsafe successor/restart |
| C01-19 | Duplicate delivery of prior control or Send identity | Earlier effects may exist | Reconcile each *original* ID independently; no duplicate effects |

## Writer rotation and counterexamples

For zero-based **project-local reply-eligible turn ordinal** t, writer = t mod 3 in fixed order Hermes Agent CLI, Kilo Code CLI, Claude Code CLI. A preceding control-only Stop does **not** advance t. Thus first later reply-eligible “Hi” uses t=0 and Hermes, then t=1 Kilo, t=2 Claude. Restart/replay retains original t/writer. Council D1 decision ordinal d is an independent counter.

**Required failure traces:** (P1) DB commit succeeded but UI ACK lost; (P2) duplicate Send identity with changed payload; (P3) Stop racing an uncertain commit and an already leased task; (P4) file changing during attachment preparation; (P5) cross-project stale response; (P6) app relaunch before new Start; (P7) hung descendant that ignores Stop. C07–C09 define their command/result, transaction and recovery grammar. The implementation must still prove those contracts with fault injection and native observations.

## Implementation-readiness boundary

The documentation choices for exact control aliases, approval identity, submission/attachment bounds, shared receipts/errors, idempotent owner transactions, replay and bounded clocks are closed by C06–C09. Command-specific payload schemas are completed per enabled implementation slice under C07 rather than accepted as unconstrained payloads. Windows CLI fencing, transaction code and fault-injection results remain implementation evidence under [C10](./C10-IMPLEMENTATION-ENTRY-GATE.md). This document is not a runtime test result.
