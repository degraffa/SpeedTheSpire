"""Per-domain header emitters.

One module per generated header, each exposing a single ``emit_*(domains) -> str``
that returns the complete header text. They share the frozen vocabularies
(``stsgen.vocab``), the loader (``stsgen.loader``) and the one effect-step parser
(``stsgen.steps``); nothing domain-specific leaks between them.
"""

from __future__ import annotations

from .cards import emit_card_table
from .encounters import emit_encounter_table
from .events import emit_event_table
from .ids import emit_ids
from .meta import emit_game_ids, emit_manifest
from .monsters import emit_monster_table
from .potions import emit_potion_table
from .powers import emit_power_table
from .relics import emit_relic_table

__all__ = [
    "emit_ids",
    "emit_card_table",
    "emit_power_table",
    "emit_relic_table",
    "emit_potion_table",
    "emit_monster_table",
    "emit_encounter_table",
    "emit_event_table",
    "emit_game_ids",
    "emit_manifest",
]
