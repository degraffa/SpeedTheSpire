# Registry

Rules-as-data source of truth (InitialPlan.md A.3, A.6): one YAML/CSV entry per
card, relic, and power, each with an effect program and a `provenance` field
citing the decompiled Java class/method it was derived from. Code-generated
into constexpr dispatch tables at build time — nothing here is hand-maintained
C++.

Empty until Stage A's registry schema is frozen.
