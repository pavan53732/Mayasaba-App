#!/usr/bin/env python3
"""Repeatable local validation of the v1 contract registry and the Screen 2 slice schemas.

Run:  python native/tests/validate_contract_registry.py

What this proves, and what it does NOT prove
--------------------------------------------
Proves: every registry-manifest digest matches the actual on-disk bytes of the file it
names; every schema is a valid JSON Schema 2020-12 document; the registry's own declared
owner-registry counts and machine-owner map agree with the schema enums; the 52 trace
fixtures and 16 council-quality fixtures validate against their declared schemas; and the
Screen 2 cross-project query definitions accept valid instances while rejecting wrong
owners, invalid IDs, oversized payloads and unsupported versions.

Does NOT prove: authority, owner-transaction atomicity, physical-effect safety, native CLI
behaviour, Windows confinement or MSI operation. Shape only. It is also not a JCS
canonicaliser proof -- see the note in check_canonical_vectors.

Exit code 0 = every check passed. Non-zero = at least one check failed.
"""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

from jsonschema import Draft202012Validator
from referencing import Registry, Resource

SCHEMA_DIR = Path(__file__).resolve().parents[2] / "docs" / "contracts" / "schemas" / "v1"

FAILURES: list[str] = []
CHECKS = 0


def check(label: str, condition: bool, detail: str = "") -> None:
    global CHECKS
    CHECKS += 1
    if not condition:
        FAILURES.append(f"{label}: {detail}" if detail else label)


def load(name: str) -> dict:
    return json.loads((SCHEMA_DIR / name).read_text(encoding="utf-8"))


def registry() -> Registry:
    """Resolve the schemas by their declared $id so relative $refs work."""
    resources = []
    for path in sorted(SCHEMA_DIR.glob("*.schema.json")):
        document = json.loads(path.read_text(encoding="utf-8"))
        identifier = document.get("$id")
        if identifier:
            resources.append((identifier, Resource.from_contents(document)))
    return Registry().with_resources(resources)


def validate(instance: dict, schema_name: str, target: str) -> list[str]:
    """Return a list of human-readable errors for `instance` against a named $defs target."""
    document = load(schema_name)
    target_schema = document["$defs"][target]
    validator = Draft202012Validator(
        {"$schema": document["$schema"], "$ref": f"{document['$id']}#/$defs/{target}"},
        registry=registry(),
    )
    # Sanity: the target must exist, otherwise the $ref above is silently meaningless.
    check(f"{schema_name}#/{target} exists", target_schema is not None)
    return [f"{e.json_path} {e.message}" for e in validator.iter_errors(instance)]


# ---------------------------------------------------------------- digest registry

def check_manifest_digests() -> None:
    manifest = load("registry-manifest.json")
    for artifact in manifest["artifacts"]:
        path = SCHEMA_DIR / artifact["file"]
        check(f"manifest artifact {artifact['file']} exists", path.is_file())
        if not path.is_file():
            continue
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        check(
            f"manifest digest for {artifact['file']}",
            actual == artifact["sha256"],
            f"recorded {artifact['sha256']} != actual {actual}",
        )
    # Every schema file on disk must be registered, so a new file cannot be silently unlisted.
    listed = {a["file"] for a in manifest["artifacts"]}
    for path in sorted(SCHEMA_DIR.glob("*.json")):
        if path.name == "registry-manifest.json":
            continue
        check(f"{path.name} is registered in the manifest", path.name in listed)


# ---------------------------------------------------------------- schema self-validity

def check_schema_documents() -> None:
    for path in sorted(SCHEMA_DIR.glob("*.schema.json")):
        document = json.loads(path.read_text(encoding="utf-8"))
        try:
            Draft202012Validator.check_schema(document)
        except Exception as error:  # noqa: BLE001 - report any meta-schema violation
            check(f"{path.name} is valid JSON Schema 2020-12", False, str(error))
        else:
            check(f"{path.name} is valid JSON Schema 2020-12", True)
        check(
            f"{path.name} declares the 2020-12 dialect",
            document.get("$schema") == "https://json-schema.org/draft/2020-12/schema",
        )


# ---------------------------------------------------------------- registry self-consistency

def check_registry_claims() -> None:
    boundaries = load("boundaries.schema.json")
    manifest = load("registry-manifest.json")
    defs = boundaries["$defs"]

    claims = (
        ("commands", "BoundaryCommand", "command_type"),
        ("events", "DomainEvent", "event_type"),
        ("records", "OwnerRecord", "record_type"),
    )
    for registry_name, definition, field in claims:
        declared = manifest["owner_registries"][registry_name]["type_count"]
        enum = defs[definition]["properties"][field]["enum"]
        check(
            f"{registry_name} type_count matches the {definition} enum",
            declared == len(enum),
            f"manifest says {declared}, schema has {len(enum)}",
        )
        check(
            f"{registry_name} enum has no duplicates",
            len(enum) == len(set(enum)),
        )

    owners = manifest["machine_owners"]
    check(
        "exactly twelve machine owners are declared",
        len(owners) == 12,
        f"found {len(owners)}",
    )
    # Every declared machine owner must be a real owner enum member.
    owner_enum = set(defs["Owner"]["enum"])
    for machine, owner in owners.items():
        check(f"machine {machine} names a declared owner", owner in owner_enum, owner)

    # The state-machine schema must itself declare exactly twelve machines, each of which
    # resolves to a real definition.
    machine_document = load("state-machines.schema.json")
    refs = [entry["$ref"] for entry in machine_document["oneOf"]]
    check(
        "state-machines.schema.json declares twelve machine records",
        len(refs) == 12,
        f"found {len(refs)}",
    )
    for ref in refs:
        name = ref.rsplit("/", 1)[-1]
        check(f"state machine {name} is defined", name in machine_document["$defs"])


# ---------------------------------------------------------------- fixtures

def check_fixtures() -> None:
    for fixture_file, schema_file, expected in (
        ("trace-fixtures.json", "trace-fixtures.schema.json", 52),
        ("council-quality-fixtures.json", "council-quality.schema.json", 16),
    ):
        fixtures = load(fixture_file)
        document = load(schema_file)
        validator = Draft202012Validator(document, registry=registry())
        errors = [f"{e.json_path} {e.message}" for e in validator.iter_errors(fixtures)]
        check(f"{fixture_file} validates against {schema_file}", not errors, "; ".join(errors[:4]))
        check(
            f"{fixture_file} carries {expected} fixtures",
            len(fixtures["fixtures"]) == expected,
            f"found {len(fixtures['fixtures'])}",
        )
        ids = [f["id"] for f in fixtures["fixtures"]]
        check(f"{fixture_file} fixture ids are unique", len(ids) == len(set(ids)))
        check(
            f"{fixture_file} status is a documentation expectation, not executed proof",
            fixtures["status"] == "DOCUMENTATION_EXPECTATION_NOT_EXECUTED_TEST",
        )


# ---------------------------------------------------------------- canonical vectors

def check_canonical_vectors() -> None:
    """Vector-content and digest self-consistency checks.

    This deliberately does NOT re-derive the canonical bytes. Re-serialising with a general
    purpose JSON writer is not a JCS proof (RFC 8785 requires ECMAScript number formatting
    and UTF-16 code-unit key ordering), and the registry README forbids claiming otherwise.
    What is checked here is that each accepted vector's stored sha256 really is the digest of
    its stored canonical bytes, that each rejected vector's raw text really contains the
    defect it names, and that each schema-targeted rejection really fails its named target.
    No canonicalizer is executed, so the `MAYASABA_JCS_1_CANONICALIZE` row is a vector-content
    check only.
    """
    vectors = load("canonical-vectors.json")
    for vector in vectors["accepted"]:
        encoded = vector["canonical_utf8"].encode("utf-8")
        digest = hashlib.sha256(encoded).hexdigest()
        check(
            f"accepted vector {vector['id']} digest matches its canonical bytes",
            digest == vector["sha256"],
            f"recorded {vector['sha256']} != actual {digest}",
        )

    def strict_load(raw: str) -> object:
        def reject_constant(name: str) -> object:
            raise ValueError(f"non-I-JSON constant {name}")

        def reject_duplicates(pairs: list[tuple[str, object]]) -> dict:
            keys = [k for k, _ in pairs]
            if len(keys) != len(set(keys)):
                raise ValueError("duplicate key")
            return dict(pairs)

        return json.loads(raw, parse_constant=reject_constant, object_pairs_hook=reject_duplicates)

    for vector in vectors["rejected"]:
        label = vector["id"]

        if "input_hex" in vector:
            raw_bytes = bytes.fromhex(vector["input_hex"])
            if vector["error_code"] == "UTF8_BOM_FORBIDDEN":
                check(
                    f"rejected vector {label} really starts with a UTF-8 BOM",
                    raw_bytes.startswith(b"\xef\xbb\xbf"),
                )
            else:
                try:
                    raw_bytes.decode("utf-8")
                except UnicodeDecodeError:
                    check(f"rejected vector {label} is not valid UTF-8", True)
                else:
                    check(f"rejected vector {label} is not valid UTF-8", False, "decoded cleanly")
            continue

        raw = vector["json_text"]

        if "schema_ref" in vector:
            schema_name, _, pointer = vector["schema_ref"].partition("#/")
            target_name = pointer.rsplit("/", 1)[-1]
            errors = validate(json.loads(raw), schema_name, target_name)
            check(
                f"rejected vector {label} fails {vector['schema_ref']}",
                bool(errors),
                "instance unexpectedly validated",
            )
            continue

        target = vector.get("validation_target", "")

        if target == "RAW_JSON_PARSE":
            if vector["error_code"] == "INVALID_UNICODE":
                # Python's json accepts a lone surrogate escape, so the defect is detected by
                # requiring the decoded string to survive strict UTF-8 encoding.
                decoded = json.loads(raw)
                values = [v for v in (decoded.values() if isinstance(decoded, dict) else [decoded])]
                encoded_cleanly = True
                for value in values:
                    try:
                        value.encode("utf-8", "strict")
                    except (UnicodeEncodeError, AttributeError):
                        encoded_cleanly = False
                check(
                    f"rejected vector {label} contains an unpaired surrogate",
                    not encoded_cleanly,
                    "encoded to UTF-8 cleanly",
                )
                continue
            try:
                strict_load(raw)
            except ValueError:
                check(f"rejected vector {label} is rejected by a strict raw parse", True)
            else:
                check(
                    f"rejected vector {label} is rejected by a strict raw parse",
                    False,
                    "strict parse accepted it",
                )
            continue

        if target == "MAYASABA_JCS_1_CANONICALIZE":
            # Vector-content check only: the canonicalizer itself is not executed here.
            check(
                f"rejected vector {label} really contains a negative-zero literal",
                "-0" in raw,
                f"raw text {raw!r} has no negative zero",
            )
            continue

        check(f"rejected vector {label} has a recognised validation_target", False, target)


# ---------------------------------------------------------------- Screen 2 slice defs

VALID_QUERY = {
    "contract_version": "1.0.0",
    "query_id": "3fa85f64-5717-4562-b3fc-2c963f66afa6",
    "projection": "RECENT_PROJECTS",
    "page_size": 20,
}

VALID_RESULT = {
    "contract_version": "1.0.0",
    "query_id": "3fa85f64-5717-4562-b3fc-2c963f66afa6",
    "projection_time_utc": "2026-10-16T12:00:00.000Z",
    "completeness": "COMPLETE",
    "items": [{"domain_id": "7ba99f64-1234-5678-b3fc-999999999999", "display_name": "Northstar"}],
}


def check_slice_defs() -> None:
    errors = validate(VALID_QUERY, "boundaries.schema.json", "CrossProjectQueryRequest")
    check("a valid cross-project query request is accepted", not errors, "; ".join(errors))
    errors = validate(VALID_RESULT, "boundaries.schema.json", "CrossProjectQueryResult")
    check("a valid cross-project query result is accepted", not errors, "; ".join(errors))

    rejections = [
        (
            "wrong owner",
            {**VALID_QUERY, "owner": "COUNCIL_ENGINE"},
            "CrossProjectQueryRequest",
        ),
        (
            "invalid query id",
            {**VALID_QUERY, "query_id": "not-a-uuid"},
            "CrossProjectQueryRequest",
        ),
        (
            "zero page size",
            {**VALID_QUERY, "page_size": 0},
            "CrossProjectQueryRequest",
        ),
        (
            "oversized page size",
            {**VALID_QUERY, "page_size": 201},
            "CrossProjectQueryRequest",
        ),
        (
            "unsupported contract version",
            {**VALID_QUERY, "contract_version": "2.0.0"},
            "CrossProjectQueryRequest",
        ),
        (
            "unknown projection",
            {**VALID_QUERY, "projection": "ALL_SECRETS"},
            "CrossProjectQueryRequest",
        ),
        (
            "error result that also carries items",
            {
                **VALID_RESULT,
                "error": {"code": "STORE_UNAVAILABLE", "owner": "PROJECT_SERVICE",
                          "message": "store unavailable", "retryable": False},
            },
            "CrossProjectQueryResult",
        ),
        (
            "unknown completeness value",
            {**VALID_RESULT, "completeness": "MAYBE"},
            "CrossProjectQueryResult",
        ),
    ]
    for label, instance, target in rejections:
        errors = validate(instance, "boundaries.schema.json", target)
        check(f"cross-project {label} is rejected", bool(errors), "instance was accepted")


def main() -> int:
    if not SCHEMA_DIR.is_dir():
        print(f"FAIL: schema directory not found at {SCHEMA_DIR}")
        return 1
    check_manifest_digests()
    check_schema_documents()
    check_registry_claims()
    check_fixtures()
    check_canonical_vectors()
    check_slice_defs()

    if FAILURES:
        print(f"FAIL: {len(FAILURES)} of {CHECKS} registry checks failed")
        for failure in FAILURES:
            print(f"  - {failure}")
        return 1
    print(f"PASS: {CHECKS} contract-registry and Screen 2 slice schema checks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
