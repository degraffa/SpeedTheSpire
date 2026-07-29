# Living Wall obtain-animation race

Source: `b14_accept`, seed `STS00009`, floor 4, Living Wall transform.

The transform removes `Defend_R` immediately (`miscRng` 0→1), while
`ShowCardAndObtainEffect.update` grants `Dark Embrace` only when its animation
finishes. Capture records 40–47 therefore contain 11 cards; record 48, the
first dump on the next floor, contains 12. The second B1.4 acceptance campaign
(`b14_accept2`) completed the same seed sweep but did not take the same Living
Wall branch; it is not claimed as a second witness.

`replay_run_diff --event` classifies the case only when every field difference
is `master_deck_count` or a `master_deck[i]` at/past the shorter deck's end.
Any stream, pool, event flag, shared-prefix card, or other state difference
remains a real divergence.

Provenance: `ShowCardAndObtainEffect.java:30-45,94-108` (constructor stores the
card; `update` calls `CardGroup.obtain` after the effect completes).
