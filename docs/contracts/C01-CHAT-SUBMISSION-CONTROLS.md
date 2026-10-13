# C01 — Chat contribution, immediate control and turn admission

**Documentation status: source-derived trace contract; versioned executable schema remains OPEN.** This pack restates the approved [Mayasaba authoritative description](../Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md): Continuous Chat, three-agent reply, immediate controls and persistence gate. It adds no project decision, service or state machine.

## Existing owners and identities

- **Message/Contribution** owns immutable UserContribution, turn identity, writer ordinal, consolidated reply and local draft links. **Project** owns binding and closure. **Work-request authority** owns independently scoped material actions. **Orchestrator/Gateway/MCF/Context** coordinate actual authorized three-agent delivery, sessions and current application proof. **SQLite Storage** commits owned state, event and outbound record atomically. **Policy and execution owners** govern effective physical effects. The **WinUI UI** only submits typed commands and projects observed results.
- Every enabled Send has one stable submission identity, complete text and selected attachment set; at most one immutable confirmed contribution may result. A valid immediate Pause/Stop has a **separate stable control-operation identity**. A confirmed reply-eligible contribution may allocate exactly one zero-based **project-local turn ordinal**. A control-only submission allocates neither turn nor writer ordinal.
- Exact JSON field types, wire IDs, error codes, construction of idempotency keys and commit transaction boundaries **remain OPEN**; names here are existing concepts, not implementation schemas.

## Send admission table

| Case | Initial fact | Only approved controller meaning | Forbidden |
| --- | --- | --- | --- |
| C01-01 | No bound authorized project root | Welcome has no composer; Open Folder supports an unsent draft and disabled Send | Saving a contribution |
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

**Failure traces that remain relevant:** (P1) DB commit succeeded but UI ACK lost; (P2) duplicate Send identity with changed payload; (P3) Stop racing an uncertain commit and an already leased task; (P4) file changing during attachment preparation; (P5) cross-project stale response; (P6) app relaunch before new Start; (P7) hung descendant that ignores Stop. The approved outcomes forbid silent duplication, stale authority or unsafe effect claims, but their exact typed command/error, atomicity and native proof are still missing.

## Implementation-readiness boundary

**Still OPEN:** P0-01/02/03/05, D-06/D-07, Z-01/Z-02/Z-03/Z-06/Z-11; control-only recognition predicate, true schema/record and error definitions, idempotency key construction, owner transaction/ACK replay, attachment size/count/deadline rules and Windows CLI effective fencing. This document is a **source-based expected trace**, **not executable JSON Schema**, **not a test result**, and **not authorization to implement a missing choice**.
