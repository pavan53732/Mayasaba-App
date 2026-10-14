# Mayasaba contract registry v1

This directory is the machine-readable documentation registry for contract version `1.0.0`. It is an implementation input, not generated application code or runtime proof.

| File | Scope |
| --- | --- |
| [`core.schema.json`](./core.schema.json) | IDs, exact timestamps/counters, authority, artifacts, errors and command receipts |
| [`boundaries.schema.json`](./boundaries.schema.json) | Owner commands, domain events, MCF envelopes, authoritative record wrapper and acceptance evidence |
| [`state-machines.schema.json`](./state-machines.schema.json) | Records for exactly twelve authoritative machines |
| [`policy.schema.json`](./policy.schema.json) | Approved storage/attachment/time bounds, executable pins, explicit bound actions, language, composer and governed-update policy |
| [`council-quality.schema.json`](./council-quality.schema.json) | C11 typed Council quality subdocuments and fixture schema; no new owner/command/state |
| [`council-quality-fixtures.json`](./council-quality-fixtures.json) | CQ01–CQ16 prospective evidence/critique-quality scenarios, not executed results |
| [`canonical-vectors.json`](./canonical-vectors.json) | `mayasaba-jcs-1` accept/reject and digest vectors |
| [`trace-fixtures.schema.json`](./trace-fixtures.schema.json) | Closed shape for the 52 adversarial fixture mappings |
| [`trace-fixtures.json`](./trace-fixtures.json) | Machine-readable mapping of C05 T01–T22 and register F01–F30 to separate boundary, physical and operational results |
| [`registry-manifest.json`](./registry-manifest.json) | Schema IDs/digests, canonical owner-registry pointers, twelve machine owners and fixture-set counts |

## Dialect and validation

The dialect is JSON Schema 2020-12 with the format-assertion vocabulary enabled for `date-time`; validators must reject invalid calendar dates in addition to enforcing the exact millisecond-UTC pattern. `$id` values are stable logical identifiers; local resolution uses the files in this directory. `core.schema.json` is a non-instantiable definitions module, so callers validate against a named `$defs` target rather than its root. All boundary objects reject undeclared top-level properties. Nested domain `payload`/`body` objects are extension points only when the command/record-specific implementation schema further constrains them. An implementation cannot claim registry completion while leaving a material payload as an unconstrained object; the wrapper schemas define the shared boundary, and each implemented command must add its typed payload schema before that command is enabled.

Parser limits apply before semantic validation: 1 MiB inline document/envelope bytes, 64 nesting levels, 10,000 members in a collection, 256 top-level properties and 64 KiB per ordinary string. Larger immutable content uses a digest-verified managed artifact reference.

## Compatibility

- Major version change: breaking field, meaning, canonical-byte or state/command change; requires material review and explicit migration.
- Minor version change: backward-compatible optional field or new enum/command that older readers must reject rather than reinterpret.
- Patch version change: documentation or validation correction that does not change accepted canonical instances.
- A producer declares exactly one version. A consumer either validates that version or returns `CONTRACT_VERSION_UNSUPPORTED`; it never guesses.
- Persisted objects retain the schema and canonical profile under which they were written. Migration creates linked successor records and does not rewrite historical hashes.

## `mayasaba-jcs-1`

The profile uses RFC 8785/JCS-compatible canonical JSON over I-JSON and UTF-8 without BOM. It rejects duplicate keys, invalid UTF-8, unpaired surrogates, NaN, infinity and negative zero. It performs no Unicode normalization. Exact/security-relevant integers and decimals that could lose precision are schema strings. Timestamps, UUIDs and SHA-256 text use the exact core-schema forms.

`canonical-vectors.json` stores canonical text and SHA-256 for accepted vectors plus the exact raw-parser, canonicalizer or named-schema target and required rejection code for rejected inputs. Schema-targeted date vectors use a validator configured with JSON Schema 2020-12 format assertion. Before the first authoritative hash, two independent serializer runs must match all accepted bytes/digests and reject every negative vector. A common library `dump()` call is not proof.

## Proof boundary

Successful JSON parsing or schema validation proves only contract shape. It does not prove authority, owner transaction atomicity, physical-effect safety, language admission, evidence adequacy, native CLI behavior, Windows confinement or MSI operation. Those gates remain explicit in C08–C10.

**C11 adds documentation-only subdocument schemas and separate prospective CQ fixtures.** Existing authoritative enum counts and T/F fixtures are preserved. Native validators and semantic oracles must prove independent evidence and D-03 precedence; JSON shape alone cannot.
