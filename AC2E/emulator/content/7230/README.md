# Build 7230 skill data

These tables are selected by the bootstrap's verified 7230 script fingerprint,
before character-store replay initializes skill catalogs. Legions and converted
7148 profiles retain their existing tables.

The active-effect and travel tables were generated from the unmodified archived
7230 `portal.dat` using `tools/import_skill_effects.py --profile 7230` and this directory's skill
catalog. `skill-effects.report.json` pins every successfully decoded skill and
effect record by SHA-256. The runtime validates those records against the supplied
archive at startup. Unresolved records remain listed in the report; selecting
7230 does not make unsupported effect types executable.

The v3 effect table preserves native stat operation codes and compound children.
Stat magnitudes use field 36 in both profiles; build 7230's vital magnitude uses
field 37. The checked archive has index fingerprint
`cb4603b96a74d6d81045a7bbd90d3058f72eb1297f6ebf3af50efda829b9dbff`.
It is distinct from Icefire's live-patched client snapshot. Coverage currently
reports 587 executable core definitions, 239 for the three playable races;
473 specialized and 29 travel definitions still require implementation.

`tests/profile_7230.rs` exercises training and casting Flame Bolt in a separate
process. Set `AC2_TEST_7230_PORTAL` to the native archive path to additionally
validate the eligibility, advancement and effect tables against the DAT.
