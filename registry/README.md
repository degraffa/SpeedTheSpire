# Registry

Rules-as-data source of truth (design doc §6; Stage B design §4). One YAML file
per domain, each entry carrying a stable numeric `id`, a `game_id` (the game's
string id — the translator/oracle join key), and a `provenance` field citing the
decompiled Java class/method it was derived from.

`tools/registry_gen/gen.py` (Python 3 + PyYAML) code-generates `constexpr`
headers from these files at build time, under the build tree
(`<build>/generated/sts/registry/*.hpp`) — **generated code is never committed**,
and the YAML is never parsed at runtime. The engine consumes the generated
headers directly: `types.hpp`/`cards.hpp`/`monster_jaw_worm.hpp` re-export the
enums and tables into `sts::engine` (the B2.2 skeleton migration — no hand
tables remain).

## Domains

| File | Generates | Notes |
|---|---|---|
| `cards.yaml` | `CardId` enum + `CardDef` effect-program table + pool tables + game_id table | full S1 scope: Ironclad red + colorless + curses, base **and** upgraded programs |
| `powers.yaml` | `PowerId` enum + hook-program table + game_id table | buff/debuff type, stacking rule, priority |
| `monsters.yaml` | `MonsterId` enum + `MonsterDef` stat/move table + game_id table | Act-1 roster incl. elites/bosses; per-ascension-tier HP/amount columns; move-selection sometimes `ai: native` |
| `relics.yaml` | `RelicId` enum + tier/hook tables + game_id table | shared pools + Ironclad starter/boss; `pool_order` is RNG-relevant |
| `potions.yaml` | `PotionId` enum + effect-program table + game_id table | Ironclad-obtainable pool |
| `events.yaml` | `EventId` enum + option-metadata table + game_id table | Act-1 `eventList` / `shrineList` / `specialOneTimeEventList` rows in canonical Java insertion order |
| `encounters.yaml` | `encounter_table.hpp` pool weights + composition programs (no enum; identity is the `game_id` string) | Act-1 pools with exclusion rules |
| `a20.yaml` | manifest row count only (no game_id; no emitter) | one provenance-backed row per ascension level; the engine implements the modifiers, tier-2 tests verify them |

Entry counts are deliberately not stated here — they go stale; re-derive with
`grep -c '^- id:' registry/<domain>.yaml`. Per-stage scope lives in the design
docs.

## Frozen schema rules (Stage B design §4.2, §4.4)

- **Explicit stable numeric ids.** Hand-assigned in the YAML, append-only, and
  never derived from file order or renumbered — traces store raw id bytes.
- **`game_id` and `provenance` are mandatory** on every entry.
- Ids are re-pinned by `static_assert` in the generated headers.
- Codegen is deterministic (sorted iteration, no timestamps): the same YAML
  produces byte-identical headers on every run.

Each entry file's header comment documents that domain's exact entry shape.
