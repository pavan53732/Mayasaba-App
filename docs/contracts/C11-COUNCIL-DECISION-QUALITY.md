# C11 — FULL Council Decision Quality Contract

**Status: NORMATIVE DOCUMENTATION REFINEMENT (2026-10-14); no native implementation or executed test claimed.** This contract sharpens the [authoritative system description](../Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md). [C02](./C02-FULL-COUNCIL-D1-D3.md) controls D1–D3 participation/outcome priority; [C06](./C06-BINDING-PRODUCT-DECISIONS.md) controls triggers/evidence; [C07](./C07-VERSIONED-CONTRACT-REGISTRY.md) owns representation; [C08](./C08-TWELVE-STATE-MACHINES.md) owns transitions; [C09](./C09-SELECTION-TRANSACTION-RECOVERY.md) owns replay. This is an additive quality gate, not permission to change any prior product decision.

## Q0 — Procedural completeness is not decision validity

The Council Engine must distinguish **(a) procedural FULL completeness** (three current independent proposals, six actual directed critiques, required rebuttals, and when synthesis is used both reviews of its exact revision), **(b) substantive quality** (evidence, critique resolution, alternative fairness, adversarial challenges and consistency), and **(c) binding authority** (separate Decision/Requirement/Policy/user approval). Neither majority, unanimous prose nor chair synthesis substitutes for evidence or authority. Quality packets are digest-bound views/subdocuments of existing owner records, never a new service, state machine or source of binding decisions.

A missing substantive critique remains **missing participation** under D-03 priority 3, including at the fifth-round/time limit; it cannot be represented as completed `cap reached`. Missing factual information, a user choice, or inadequate evidence follow D-03 priorities 4, 5 and 6 respectively. Do not append an eleventh outcome rule or reorder any existing priority.

## Q1 — Claims, facts, constraints and inferences

Each consequential claim has immutable identity, author/position/revision, context and approved epoch, class, load-bearing flag, exact evidence/authority references, premises, contradictory evidence, adequacy state and a concise checkable rationale (never private chain of thought).

| Class | Necessary proof |
| --- | --- |
| `EMPIRICAL_OBSERVATION` | Actually observed reproducible artifact/environment evidence, identity, freshness and source provenance; not an agent assertion |
| `AUTHORITATIVE_CONSTRAINT` | Current approved requirement, policy, user instruction, platform limit or lock reference |
| `LOGICAL_INFERENCE` | Explicit checkable inference and adequate linked premises; challenge counterexamples |
| `USER_PREFERENCE` | Provenance to exact user/approved preference; never promoted to objective measurement |
| `ASSUMPTION` | Explicitly unverified; cannot supply positive load-bearing feasibility/safety proof |

Evidence state: `SUPPORTED`, `INCONCLUSIVE`, `CONTRADICTED` or `STALE`. Multiple citations to one experiment/log are one observation. Evidence/Validation must independently check relevance, independence, observation adequacy and current identity; unsupported precision, numeric confidence and speculative weight are not proof. A credible observed counterexample overrides unanimous agent agreement.

## Q2 — The exact six-slot critique-to-claim matrix

Each of the six frozen, distinct reviewer→author assignments requires an *actual attributed substantive critique* targeting the assigned frozen position, material claim or assumption, and containing a falsifiable check where feasible or a defensible explicit reason checking is unavailable/unnecessary. No self-critique, ACK, blank approval, paraphrase, duplicate or old-context critique fills a slot. An authored no-defect finding may count only when it identifies actual constraints/claims examined and checks performed; a rubber stamp does not.

Before `SubmitSynthesis`, Council Engine produces a **Claim-to-Critique Coverage Matrix** with all six exact directed identities, target claim/revision/source links, rebuttals, observable evidence, missing checks, risk/decision impact and one of:

| Disposition | Required support |
| --- | --- |
| `RESOLVED_BY_REVISION` | Exact immutable successor revision with relevant evidence/criteria rechecked |
| `REFUTED_BY_EVIDENCE` | Adequate *independent* observation invalidates critique's factual claim |
| `SURVIVING_TRADE_OFF` | True remaining preference, cost or reversibility compromise, not a fictitious resolution |
| `UNRESOLVED_FACT` | Material absent factual prerequisite, linked to current information need |
| `UNRESOLVED_CHALLENGE` | Material unanswered/unsupported objection preventing positive certification |

An invalid critique is not a sixth disposition: its mandatory assignment stays incomplete. Missing/misattributed matrix rows prevent `SubmitSynthesis`, not merely a warning. Any materially unresolved fact/challenge cannot be hidden by synthesis prose. Both nonchair reviewers inspect the **same exact synthesis *and coverage revision digest*** plus sources; any changed revision invalidates both prior reviews. Controller coverage checks source identities and contradictions independently of the peers' approval wording.

## Q3 — Adversarial reasoning without manufactured disputes

The rotating skeptic should target the highest-impact *shared* assumption (a common error can fool all three agents), including failure/compatibility/security/counterfactual scenarios. The verifier checks load-bearing premises and negative evidence. Both still make independent proposals and critique both peers: no seventh directed critique or new role. Prioritize bounded discriminating observations under approved scope and existing execution/policy gates; record reasons when an objective check is unsafe, impossible or irrelevant. Never invent a failed test, fake divergence, measured score or forced research authority.

Round 1 is blind before proposal freeze. In later same-baseline rounds, agents intentionally see history and explicitly reaffirm/revise; those are **not** fresh independent proposals. Material context/epoch change requires a fresh blind set of three on the new applied baseline. Neither that nor resumed/replayed work resets the five-round point budget or project-local role ordinal. Pure rhetorical variation is not substantive progress. Fixpoint requires the original two comparable completed rounds plus positive substantive/authority gates.

## Q4 — Fair evaluation of all viable alternatives

Use the existing `DecisionEvaluationMatrix` to compare current design and other viable alternatives against the *same applicable* approved requirements, safety constraints and criteria. Document cost, scalability/performance, isolation/security, reliability, failure recovery, maintainability, compatibility, operating burden, reversibility, limitations, concrete evidence and sensitivity to assumptions where relevant. An option is excluded only by a proven authoritative violation or impossibility, not by rhetoric, majority or invented weighted rank. Subjective values remain choices with user provenance. Preserve three or more material alternatives when appropriate: a binary UI shortcut is not a reason to suppress them.

## Q5 — Cross-decision validity and grounded historical lessons

Before eligible synthesis/convergence, trace applicable prior HARD_LOCKs, scope, acceptance-criterion conflicts, approved requirement versions and downstream task/artifact effects. Existing Decision service alone validates successor decisions. A material contradiction or changed evidence yields `DecisionValidityAssessment` and scoped quarantine/authorized reopening as already specified; it never silently reverses authority or rewrites history. Later councils may consult retained failures, counterexamples and verified outcomes as **read-only investigation leads**, not as learned authority, self-training or new provider/model selection. Preserve unaffected authorized work.

## Q6 — Substantive outcome and concise escalation

A positive `synthesized`/`converged` candidate requires all mandatory contributions, six real justified coverage rows, currently adequate load-bearing evidence, no unaddressed blocking objection, valid common-alternative comparison, current prior-decision compatibility and exact dual reviews when synthesized. The **ten-row D-03 priority** still decides first applicable outcome: e.g. fact before preference, inadequate evidence before synthesis, a complete unresolved cap only after earlier guards. `BLOCKED`, `PAUSED` and internal `CONTINUE` remain dispositions, not sixth/seventh external outcomes. The Council itself never grants user approval.

The existing Chat card explains the scoped question, viable alternatives, hard constraints, strongest objections, evidence/uncertainty, consequence/risk/reversibility, phase/outcome and one *permitted* next action; an unresolved fact, user choice and exhausted cap are distinguished. Keep full six-slot matrix and source evidence in Show details; never dump five debate transcripts or reveal private reasoning. Do not force two buttons when three options or factual investigation are necessary.

## Q7 — Typed subdocuments, semantic guards and prospective fixtures

[`council-quality.schema.json`](./schemas/v1/council-quality.schema.json) defines closed `ClaimQuality`, `DirectedCritiqueCoverage`, `SynthesisQualityPacket`, `ChoiceExplanationPacket` structures **inside existing Council/Evidence/Decision owner records**. These are not additional owner-record/command/event enum values. Cross-record semantic validators must separately prove all six distinct directed edges, nonempty authored work, exact current source identity, independent evidence, rebuttal adequacy, exact review digest and D-03 precedence; schema shape alone cannot.

[`council-quality-fixtures.json`](./schemas/v1/council-quality-fixtures.json) contains CQ01–CQ16: polite rubber stamps, missing/hollow critiques, chair misquotation, stale reviews, invented measurement, invalid inference, source correlation, unanimous false premise, subjective trade-off, multiway choice, factual-before-choice, superficial later-round consensus, contaminated new context, decision validity and valid synthesis. **These are expected scenarios, not executed tests.** Native test/validation results must be obtained before any claim of quality-gate completion.

**Compatibility invariant:** exactly three CLIs, six directed critiques, existing D1 chair rotation, five rounds per point, five public D-03 outcomes and ten precedence rows, thirteen layers, twelve state machines, current permission/approval gates, and one Chat route. No new council mode, agent, model/provider override, page, owner, command, state or authority.

