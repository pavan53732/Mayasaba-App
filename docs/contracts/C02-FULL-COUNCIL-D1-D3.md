# C02 — FULL council D1, D2 and D3 trace contract

**Status: derived documentation / executable transition schemas OPEN.** Source: [authoritative system description — FULL council and D-03](../Mayasaba%20%E2%80%94%20Complete%20System%20Description%20%28for%20AI%20agents%29.md). This table is subordinate and does not reopen existing approved council rules or select D-04 materiality.

## Project-local D1 role identity

Council Engine durably and atomically allocates zero-based, **project-local** decision-point creation ordinal d. For one-based FULL round number r, choose i=(d+r−1) mod 3 over index 0 Hermes, 1 Kilo Code, 2 Claude Code. Chair/proposer=i, skeptic=(i+1) mod 3, verifier=(i+2) mod 3. Assign all before proposals; proposal content, agent preference, arrival order or replay cannot change the roles.

| Project | d | r | Chair/proposer | Skeptic | Verifier |
| --- | --- | --- | --- | --- | --- |
| A | 0 | 1 | Hermes | Kilo | Claude |
| A | 0 | 2 | Kilo | Claude | Hermes |
| A | 1 | 1 | Kilo | Claude | Hermes |
| B | 0 | 1 | Hermes | Kilo | Claude |
| A | 2 | 2 | Hermes | Kilo | Claude |

Creating B's first point after several A points still yields B d=0. Concurrent creation in A must allocate distinct ordinals atomically. Existing point identities/ordinals remain stable through restart, replay and reopening. The conversational writer's turn ordinal is unrelated.

## FULL participation and completeness

| Required proof | When missing/invalid |
| --- | --- |
| Frozen authorized baseline actually applied to all three CLI sessions, not merely transport receipt | Withhold dependent FULL work |
| Three independently authored current proposals before peer disclosure (later rounds' explicit reaffirmations/revisions where specified) | Incomplete |
| Six *actual directed critiques* — each CLI critiques both other participants' frozen proposals, never self | Incomplete; silence, ACK, abstention or assumed critique cannot fill slot |
| Required authored rebuttals and valid predecessor-linked revisions | Incomplete |
| When synthesis evaluated, chair candidate cites all surviving positions and both nonchair agents review the **same exact synthesis revision/digest** | Incomplete if either review missing/stale; chair is not an authority |
| Independent source/evidence, current context, positive gates, remaining conflicts and policy | No positive verdict from unsupported assumptions or three-agent consensus alone |

A changed synthesis invalidates both previous reviews. One missing review is not a majority-approved synthesis. Council completion never itself supplies required **user** approval.

## D3 first-applicable outcome vectors

Apply the authoritative ten-row priority table; rows below are adversarial *cross-products*, not replacement rules.

| Case | Initial competing facts | Controller's required result |
| --- | --- | --- |
| D3-01 | Authorized emergency Stop while critiques absent | Priority 1 — Stop acts immediately, missing work recorded |
| D3-02 | All proposals/critiques available but stale epoch or unsafe effect | Priority 2 — BLOCKED; no positive result |
| D3-03 | Fifth round timed out, one critique absent, factual question also unresolved | Priority 3 — still incomplete; BLOCKED at bound, **not** cap reached or open-question seal |
| D3-04 | Synthesis has only one nonchair exact-digest review, cap exhausted | Priority 3 — incomplete, never synthesized or completed cap |
| D3-05 | Completed FULL round with factual dependency and user trade-off both open | Priority 4 — sealed with an open question (fact before preference) |
| D3-06 | Completed round with only user preference/choice unresolved | Priority 5 — escalated choice packet; silence never consent |
| D3-07 | Completed unanimous round but evidence for load-bearing claim insufficient | Priority 6 — bounded investigation or BLOCKED, no positive result |
| D3-08 | Complete reviewed synthesis, positive gates and source evidence satisfied | Priority 7 — synthesized |
| D3-09 | First stable complete round, no accepted synthesis, budget remains | Priority 10 — CONTINUE(CONVERGENCE_CONFIRMATION_PENDING), not convergence |
| D3-10 | Second consecutive comparable stable complete round, all gates positive | Priority 8 — converged unless eligible synthesis already accepted |
| D3-11 | Completed unresolved round at fifth-round cap, earlier guards cleared | Priority 9 — cap reached, preserve unresolved issues |
| D3-12 | Complete unresolved round, valid continuation cause, budget remaining | Priority 10 — one recorded CONTINUE |
| D3-13 | Complete unresolved round, no permitted cause to continue | Priority 10 fallback — BLOCKED; never spin |
| D3-14 | Otherwise positive, still pending user approval | Wait at existing approval gate, no inferred authorization |
| D3-15 | Context materially changes between otherwise stable rounds | Not comparable for convergence; do not reset the point round cap |

The **five externally reported outcomes** remain exactly: converged, synthesized, cap reached, escalated, sealed with an open question. CONTINUE is internal/nonterminal; PAUSED/BLOCKED are operating dispositions, not sixth/seventh council outcome values.

## Concurrent and replay cases (expected, not executed)

- Concurrent same-project point creation must not allocate one d twice; duplicate trigger identity must not manufacture an additional FULL point.
- Same point/round replay must preserve its roles, identities and prior valid contributions.
- Late directed critique or synthesis review for a superseded target/digest cannot satisfy mandatory participation.
- A completed round is not an approved plan. The owning decision/user gate independently checks evidence and authority.
- A first complete round can establish comparison baseline, but cannot meet the *two consecutive comparable* convergence condition.

## Implementation-readiness boundary

**Still OPEN:** D-04 trigger/materiality registry, duplicate-key construction, normalized surviving-position equality and baseline comparison algorithm, typed Council machine #7 state/command/event/error/transaction, exact bounds, D-15 objective evidence sufficiency and fresh independence after peer material disclosure. The D1/D2/D3 rules are approved; their machine-readable and physical verification obligations remain open. No application implementation or runtime test is claimed.
