# Twin fixtures — the leak-gate export

`twins_v1.bin` is the T0.5 export the training repo consumes
([../../../docs/training-plan.md](../../../docs/training-plan.md) §2.6a; the
consumer is task T1.6, which gates policy-logit and search-statistic invariance
on it).

Each case is a **recipe plus a payload**:

- **recipe** — `(run_seed, ascension, policy, policy_seed, action prefix,
  twin_seed)`. Replaying the prefix from `run_begin(run_seed, ascension)` gives
  the TRUE state; `make_hidden_twin(truth, twin_seed)`
  ([../../../include/sts/engine/twin.hpp](../../../include/sts/engine/twin.hpp))
  gives its twin.
- **payload** — the `PublicView` (mask channel included) that BOTH of them
  encode to, stored verbatim.

It is a recipe rather than a state dump because `RunController` cannot be
written as bytes: `MonsterLists` holds `std::string_view` encounter keys, so a
controller's representation contains pointers whose values move with ASLR. That
is also the reconstruction-by-replay doctrine the training plan already uses for
restricted sidecars (plan §5).

**The invariance property a consumer should assert:** any function of the
observation — encoder output, policy logits, search statistics at a pinned
sampler seed — must be identical on the two rebuilt states of every case. They
differ only in hidden state, and every stored payload is byte-identical between
them by construction.

## Format, generation, and refusal

The byte layout, the version stamps, and the refuse-on-mismatch rules are
specified in
[../../../tools/twin_fixtures/include/sts/twin/twin_fixture.hpp](../../../tools/twin_fixtures/include/sts/twin/twin_fixture.hpp).
A loader refuses any file whose magic, format version, engine `SCHEMA_VERSION`,
`PUBLIC_VIEW_VERSION` or either struct size differs from the reading build.

Regenerate — never hand-edit — with the checked-in generator, exactly as the
combat fixtures are regenerated:

```bash
cmake --build --preset win-debug --target gen_twin_fixtures
build/win-debug/bin/gen_twin_fixtures
```

The generator verifies every case by replay before it writes, and
`twin_fixture_test` re-verifies the committed file on every build: each case is
rebuilt from its recipe and must reproduce the stored view, and so must its
twin.
