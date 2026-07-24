"""SpeedTheSpire registry codegen library (design doc §4.3).

``gen.py`` is a thin driver over this package:

  * ``vocab``  -- the frozen vocabularies mirroring the engine headers, plus
                  ``GenError``/``fail`` and the small text helpers.
  * ``loader`` -- YAML loading and the domain-independent id/name/provenance
                  validation pass.
  * ``steps``  -- the ONE effect-step parser: the global `extra` packing table
                  plus each domain's op allowlist.
  * ``emit``   -- one module per generated header.

Split out of a 2,219-line gen.py so a registry batch task edits one domain's
emitter instead of the file every other batch task is also editing.
"""

from __future__ import annotations

from .generate import generate, write_if_changed
from .vocab import GenError

__all__ = ["generate", "write_if_changed", "GenError"]
