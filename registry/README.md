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
| `cards.yaml` | `CardId` enum + `CardDef` effect-program table + game_id table | 5 skeleton cards |
| `powers.yaml` | `PowerId` enum + game_id table | 3 skeleton powers |
| `monsters.yaml` | `MonsterId` enum + `MonsterDef` stat/move table + game_id table | Jaw Worm (per-ascension-tier HP/amount columns; `ai: native`) |
| `relics.yaml` | `RelicId` enum + game_id table | empty (skeleton has none) |
| `potions.yaml` | `PotionId` enum + game_id table | empty |
| `events.yaml` | `EventId` enum + game_id table | empty |
| `encounters.yaml` | manifest row count only | empty |
| `a20.yaml` | manifest row count only | empty (A20 table deferred) |

## Frozen schema rules (Stage B design §4.2, §4.4)

- **Explicit stable numeric ids.** Hand-assigned in the YAML, append-only, and
  never derived from file order or renumbered — traces store raw id bytes.
- **`game_id` and `provenance` are mandatory** on every entry.
- Ids are re-pinned by `static_assert` in the generated headers.
- Codegen is deterministic (sorted iteration, no timestamps): the same YAML
  produces byte-identical headers on every run.

Each entry file's header comment documents that domain's exact entry shape.
