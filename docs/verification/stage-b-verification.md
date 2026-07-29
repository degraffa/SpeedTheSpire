# Stage B verification report

Generated deterministically by `tools/verify_report/generate_report.py`.
This is evidence accounting, not a G7 acceptance inference.

## G7 oracle evidence (literal)

- Distinct seeds: **300**; shortfall to 2,000: **1700**.
- Captured actions: **16094**.
- Replay-clean actions: **12888**.
- Strict zero-diff actions: **12815**; shortfall to 1,000,000: **987185**.
- Untriaged findings: **33**. A finding counts as triaged only when an exact campaign/seed/classification disposition exists.
- Oracle volume criterion met: **NO**.

## Diff rates

- State-divergence runs per million captured actions: **1429.104** (23 findings).
- All non-clean runs per million captured actions: **2920.343** (47 findings).

## Divergence inventory

| Campaign | Seed | Classification | Disposition | Reference |
|---|---|---|---|---|
| b52_accept_locked_20260729_71000_71049 | STS71009 | replay_harness_error | open-harness-gap | ../stage-b-tasks.md#deferred-obligations |
| b52_accept_locked_20260729_71000_71049 | STS71015 | replay_harness_error | open-harness-gap | ../stage-b-tasks.md#deferred-obligations |
| b52_accept_locked_20260729_71000_71049 | STS71017 | state_divergence | open-product-divergence | ../stage-b-tasks.md#deferred-obligations |
| b52_accept_locked_20260729_71000_71049 | STS71018 | state_divergence | open-product-divergence | ../stage-b-tasks.md#deferred-obligations |
| b52_accept_locked_20260729_71000_71049 | STS71022 | replay_harness_error | open-harness-gap | ../stage-b-tasks.md#deferred-obligations |
| b52_accept_locked_20260729_71000_71049 | STS71025 | state_divergence | standing-deviation | ../stage-b-tasks.md#deferred-obligations |
| b52_accept_locked_20260729_71000_71049 | STS71030 | replay_harness_error | open-harness-gap | ../stage-b-tasks.md#deferred-obligations |
| b52_accept_locked_20260729_71000_71049 | STS71033 | state_divergence | open-product-divergence | ../stage-b-tasks.md#deferred-obligations |
| b52_accept_locked_20260729_71000_71049 | STS71039 | state_divergence | open-product-divergence | ../stage-b-tasks.md#deferred-obligations |
| b52_accept_locked_20260729_71000_71049 | STS71040 | replay_harness_error | open-harness-gap | ../stage-b-tasks.md#deferred-obligations |
| b52_accept_20260729_70000_70049 | STS70011 | state_divergence | open-product-divergence | ../stage-b-tasks.md#deferred-obligations |
| b52_accept_20260729_70000_70049 | STS70022 | replay_harness_error | open-harness-gap | ../stage-b-tasks.md#deferred-obligations |
| b52_accept_20260729_70000_70049 | STS70023 | state_divergence | open-product-divergence | ../stage-b-tasks.md#deferred-obligations |
| b52_accept_20260729_70000_70049 | STS70037 | state_divergence | open-product-divergence | ../stage-b-tasks.md#deferred-obligations |
| b53_full_act1_20260729 | STS100010 | state_divergence | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100012 | replay_harness_error | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100013 | state_divergence | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100017 | replay_harness_error | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100020 | state_divergence | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100021 | state_divergence | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100022 | state_divergence | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100045 | state_divergence | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100055 | state_divergence | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100056 | replay_harness_error | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100061 | state_divergence | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100085 | state_divergence | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100086 | replay_harness_error | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100088 | state_divergence | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100092 | replay_harness_error | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100094 | replay_harness_error | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100106 | replay_harness_error | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100107 | replay_harness_error | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100111 | replay_harness_error | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100116 | replay_harness_error | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100123 | state_divergence | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100128 | state_divergence | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100134 | replay_harness_error | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100137 | replay_harness_error | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100147 | replay_harness_error | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100154 | replay_harness_error | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100156 | replay_harness_error | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100164 | replay_harness_error | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100177 | replay_harness_error | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100186 | state_divergence | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100189 | state_divergence | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100190 | replay_harness_error | **UNTRIAGED** |  |
| b53_full_act1_20260729 | STS100195 | state_divergence | **UNTRIAGED** |  |

Open dispositions remain open defects or harness gaps; disposition is not acceptance.

## Registry coverage and oracle sightings

- Tier-2 rows covered: **452 / 452**.
- Rows with a `game_id` and zero oracle sightings: **28**.

| Domain | ID | Row | game_id | Oracle sightings | Tier-2 |
|---|---:|---|---|---:|---|
| cards | 1 | STRIKE | Strike_R | 131080 | direct |
| cards | 2 | DEFEND | Defend_R | 104106 | direct |
| cards | 3 | BASH | Bash | 53498 | direct |
| cards | 4 | SHRUG_IT_OFF | Shrug It Off | 1636 | direct |
| cards | 5 | POMMEL_STRIKE | Pommel Strike | 2030 | direct |
| cards | 6 | ARMAMENTS | Armaments | 1380 | direct |
| cards | 7 | FLEX | Flex | 1470 | direct |
| cards | 8 | HAVOC | Havoc | 832 | direct |
| cards | 9 | TRUE_GRIT | True Grit | 670 | direct |
| cards | 10 | WARCRY | Warcry | 1144 | direct |
| cards | 11 | ANGER | Anger | 1530 | direct |
| cards | 12 | BODY_SLAM | Body Slam | 1260 | direct |
| cards | 13 | CLASH | Clash | 1132 | direct |
| cards | 14 | CLEAVE | Cleave | 1518 | direct |
| cards | 15 | CLOTHESLINE | Clothesline | 1156 | direct |
| cards | 16 | HEADBUTT | Headbutt | 1182 | direct |
| cards | 17 | HEAVY_BLADE | Heavy Blade | 1947 | direct |
| cards | 18 | IRON_WAVE | Iron Wave | 504 | direct |
| cards | 19 | PERFECTED_STRIKE | Perfected Strike | 1347 | direct |
| cards | 20 | SWORD_BOOMERANG | Sword Boomerang | 448 | direct |
| cards | 21 | THUNDERCLAP | Thunderclap | 1254 | direct |
| cards | 22 | TWIN_STRIKE | Twin Strike | 788 | direct |
| cards | 23 | WILD_STRIKE | Wild Strike | 1396 | direct |
| cards | 24 | WOUND | Wound | 384 | direct |
| cards | 25 | BURN | Burn | 10 | direct |
| cards | 26 | DAZED | Dazed | 2760 | direct |
| cards | 27 | SLIMED | Slimed | 6164 | direct |
| cards | 28 | VOID | Void | 0 | direct |
| cards | 29 | CLUMSY | Clumsy | 154 | direct |
| cards | 30 | DECAY | Decay | 670 | direct |
| cards | 31 | DOUBT | Doubt | 346 | direct |
| cards | 32 | INJURY | Injury | 1318 | direct |
| cards | 33 | NORMALITY | Normality | 206 | direct |
| cards | 34 | PAIN | Pain | 256 | direct |
| cards | 35 | PARASITE | Parasite | 506 | direct |
| cards | 36 | REGRET | Regret | 568 | direct |
| cards | 37 | SHAME | Shame | 800 | direct |
| cards | 38 | WRITHE | Writhe | 392 | direct |
| cards | 39 | ASCENDERS_BANE | AscendersBane | 26917 | direct |
| cards | 40 | BLOOD_FOR_BLOOD | Blood for Blood | 972 | direct |
| cards | 41 | CARNAGE | Carnage | 674 | direct |
| cards | 42 | DROPKICK | Dropkick | 770 | direct |
| cards | 43 | HEMOKINESIS | Hemokinesis | 188 | direct |
| cards | 44 | PUMMEL | Pummel | 398 | direct |
| cards | 45 | RAMPAGE | Rampage | 704 | direct |
| cards | 46 | RECKLESS_CHARGE | Reckless Charge | 518 | direct |
| cards | 47 | SEARING_BLOW | Searing Blow | 234 | direct |
| cards | 48 | SEVER_SOUL | Sever Soul | 811 | direct |
| cards | 49 | UPPERCUT | Uppercut | 374 | direct |
| cards | 50 | WHIRLWIND | Whirlwind | 54 | direct |
| cards | 51 | BATTLE_TRANCE | Battle Trance | 710 | direct |
| cards | 52 | BLOODLETTING | Bloodletting | 118 | direct |
| cards | 53 | BURNING_PACT | Burning Pact | 320 | direct |
| cards | 54 | DISARM | Disarm | 1004 | direct |
| cards | 55 | DUAL_WIELD | Dual Wield | 462 | direct |
| cards | 56 | ENTRENCH | Entrench | 678 | direct |
| cards | 57 | FLAME_BARRIER | Flame Barrier | 588 | direct |
| cards | 58 | GHOSTLY_ARMOR | Ghostly Armor | 352 | direct |
| cards | 59 | INFERNAL_BLADE | Infernal Blade | 688 | direct |
| cards | 60 | INTIMIDATE | Intimidate | 644 | direct |
| cards | 61 | POWER_THROUGH | Power Through | 312 | direct |
| cards | 62 | RAGE | Rage | 36 | direct |
| cards | 63 | SECOND_WIND | Second Wind | 140 | direct |
| cards | 64 | SEEING_RED | Seeing Red | 828 | direct |
| cards | 65 | SENTINEL | Sentinel | 868 | direct |
| cards | 66 | SHOCKWAVE | Shockwave | 328 | direct |
| cards | 67 | SPOT_WEAKNESS | Spot Weakness | 466 | direct |
| cards | 68 | COMBUST | Combust | 752 | direct |
| cards | 69 | DARK_EMBRACE | Dark Embrace | 410 | direct |
| cards | 70 | EVOLVE | Evolve | 354 | direct |
| cards | 71 | FEEL_NO_PAIN | Feel No Pain | 342 | direct |
| cards | 72 | FIRE_BREATHING | Fire Breathing | 280 | direct |
| cards | 73 | INFLAME | Inflame | 266 | direct |
| cards | 74 | METALLICIZE | Metallicize | 834 | direct |
| cards | 75 | RUPTURE | Rupture | 560 | direct |
| cards | 76 | BARRICADE | Barricade | 82 | direct |
| cards | 77 | BERSERK | Berserk | 26 | direct |
| cards | 78 | BLUDGEON | Bludgeon | 800 | direct |
| cards | 79 | BRUTALITY | Brutality | 626 | direct |
| cards | 80 | CORRUPTION | Corruption | 240 | direct |
| cards | 81 | DEMON_FORM | Demon Form | 620 | direct |
| cards | 82 | DOUBLE_TAP | Double Tap | 942 | direct |
| cards | 83 | EXHUME | Exhume | 1334 | direct |
| cards | 84 | FEED | Feed | 396 | direct |
| cards | 85 | FIEND_FIRE | Fiend Fire | 562 | direct |
| cards | 86 | IMMOLATE | Immolate | 220 | direct |
| cards | 87 | IMPERVIOUS | Impervious | 18 | direct |
| cards | 88 | JUGGERNAUT | Juggernaut | 348 | direct |
| cards | 89 | LIMIT_BREAK | Limit Break | 576 | direct |
| cards | 90 | OFFERING | Offering | 402 | direct |
| cards | 91 | REAPER | Reaper | 180 | direct |
| cards | 92 | BANDAGE_UP | Bandage Up | 4 | direct |
| cards | 93 | BLIND | Blind | 26 | direct |
| cards | 94 | DARK_SHACKLES | Dark Shackles | 554 | direct |
| cards | 95 | DEEP_BREATH | Deep Breath | 220 | direct |
| cards | 96 | DISCOVERY | Discovery | 192 | direct |
| cards | 97 | DRAMATIC_ENTRANCE | Dramatic Entrance | 30 | direct |
| cards | 98 | ENLIGHTENMENT | Enlightenment | 94 | direct |
| cards | 99 | FINESSE | Finesse | 62 | direct |
| cards | 100 | FLASH_OF_STEEL | Flash of Steel | 218 | direct |
| cards | 101 | FORETHOUGHT | Forethought | 46 | direct |
| cards | 102 | GOOD_INSTINCTS | Good Instincts | 164 | direct |
| cards | 103 | IMPATIENCE | Impatience | 274 | direct |
| cards | 104 | JACK_OF_ALL_TRADES | Jack Of All Trades | 7 | direct |
| cards | 105 | MADNESS | Madness | 472 | direct |
| cards | 106 | MIND_BLAST | Mind Blast | 16 | direct |
| cards | 107 | PANACEA | Panacea | 216 | direct |
| cards | 108 | PANIC_BUTTON | PanicButton | 25 | direct |
| cards | 109 | PURITY | Purity | 22 | direct |
| cards | 110 | SWIFT_STRIKE | Swift Strike | 22 | direct |
| cards | 111 | TRIP | Trip | 22 | direct |
| cards | 112 | APOTHEOSIS | Apotheosis | 30 | direct |
| cards | 113 | CHRYSALIS | Chrysalis | 194 | direct |
| cards | 114 | HAND_OF_GREED | HandOfGreed | 5 | direct |
| cards | 115 | MAGNETISM | Magnetism | 166 | direct |
| cards | 116 | MASTER_OF_STRATEGY | Master of Strategy | 224 | direct |
| cards | 117 | MAYHEM | Mayhem | 134 | direct |
| cards | 118 | METAMORPHOSIS | Metamorphosis | 232 | direct |
| cards | 119 | PANACHE | Panache | 134 | direct |
| cards | 120 | SADISTIC_NATURE | Sadistic Nature | 174 | direct |
| cards | 121 | SECRET_TECHNIQUE | Secret Technique | 26 | direct |
| cards | 122 | SECRET_WEAPON | Secret Weapon | 32 | direct |
| cards | 123 | THE_BOMB | The Bomb | 22 | direct |
| cards | 124 | THINKING_AHEAD | Thinking Ahead | 18 | direct |
| cards | 125 | TRANSMUTATION | Transmutation | 124 | direct |
| cards | 126 | VIOLENCE | Violence | 22 | direct |
| cards | 127 | CURSE_OF_THE_BELL | CurseOfTheBell | 165 | direct |
| powers | 1 | STRENGTH | Strength | 9300 | direct |
| powers | 2 | VULNERABLE | Vulnerable | 3550 | direct |
| powers | 3 | WEAK | Weakened | 2620 | direct |
| powers | 4 | ARTIFACT | Artifact | 982 | direct |
| powers | 5 | METALLICIZE | Metallicize | 834 | direct |
| powers | 6 | FEEL_NO_PAIN | Feel No Pain | 342 | direct |
| powers | 7 | DARK_EMBRACE | Dark Embrace | 410 | direct |
| powers | 8 | COMBUST | Combust | 752 | direct |
| powers | 9 | RUPTURE | Rupture | 560 | direct |
| powers | 10 | SADISTIC | Sadistic | 28 | direct |
| powers | 11 | CORRUPTION | Corruption | 240 | direct |
| powers | 12 | RAGE | Rage | 36 | direct |
| powers | 13 | LOSE_STRENGTH | Flex | 1470 | direct |
| powers | 14 | DEXTERITY | Dexterity | 72 | direct |
| powers | 15 | LOSE_DEXTERITY | DexLoss | 0 | direct |
| powers | 16 | THORNS | Thorns | 26 | direct |
| powers | 17 | PLATED_ARMOR | Plated Armor | 0 | direct |
| powers | 18 | REGEN | Regeneration | 0 | direct |
| powers | 19 | RITUAL | Ritual | 3786 | direct |
| powers | 20 | CURL_UP | Curl Up | 2052 | direct |
| powers | 21 | FRAIL | Frail | 1120 | direct |
| powers | 22 | SPLIT | Split | 94 | direct |
| powers | 23 | NEXT_TURN_BLOCK | Next Turn Block | 0 | direct |
| powers | 24 | NO_DRAW | No Draw | 36 | direct |
| powers | 25 | FLAME_BARRIER | Flame Barrier | 588 | direct |
| powers | 26 | EVOLVE | Evolve | 354 | direct |
| powers | 27 | FIRE_BREATHING | Fire Breathing | 280 | direct |
| powers | 28 | BUFFER | Buffer | 18 | direct |
| powers | 29 | INTANGIBLE | IntangiblePlayer | 0 | direct |
| powers | 33 | ANGER | Anger | 1530 | direct |
| powers | 40 | ANGRY | Angry | 0 | direct |
| powers | 45 | MODE_SHIFT | Mode Shift | 0 | direct |
| powers | 46 | SHARP_HIDE | Sharp Hide | 0 | direct |
| powers | 48 | BARRICADE | Barricade | 82 | direct |
| powers | 49 | BERSERK | Berserk | 26 | direct |
| powers | 50 | BRUTALITY | Brutality | 626 | direct |
| powers | 51 | DEMON_FORM | Demon Form | 620 | direct |
| powers | 52 | DOUBLE_TAP | Double Tap | 942 | direct |
| powers | 53 | JUGGERNAUT | Juggernaut | 348 | direct |
| powers | 59 | CONFUSION | Confusion | 544 | direct |
| powers | 73 | ENTANGLE | Entangled | 0 | direct |
| powers | 74 | SPORE_CLOUD | Spore Cloud | 128 | direct |
| powers | 75 | THIEVERY | Thievery | 96 | direct |
| powers | 77 | NO_BLOCK | NoBlockPower | 2 | direct |
| powers | 78 | SHACKLED | Shackled | 22 | direct |
| powers | 81 | MAYHEM | Mayhem | 134 | direct |
| powers | 82 | MAGNETISM | Magnetism | 166 | direct |
| powers | 83 | PANACHE | Panache | 134 | direct |
| powers | 84 | THE_BOMB | TheBomb | 0 | direct |
| powers | 87 | VIGOR | Vigor | 4 | direct |
| powers | 88 | PEN_NIB | Pen Nib | 16418 | direct |
| powers | 91 | REGENERATE_MONSTER | Regenerate | 0 | direct |
| powers | 92 | DUPLICATION | DuplicationPower | 1 | direct |
| monsters | 1 | JAW_WORM | JawWorm | 6696 | direct |
| monsters | 2 | CULTIST | Cultist | 16990 | direct |
| monsters | 3 | LOUSE_NORMAL | FuzzyLouseNormal | 3226 | direct |
| monsters | 4 | LOUSE_DEFENSIVE | FuzzyLouseDefensive | 3988 | direct |
| monsters | 5 | SPIKE_SLIME_SMALL | SpikeSlime_S | 2338 | direct |
| monsters | 6 | SPIKE_SLIME_MEDIUM | SpikeSlime_M | 2666 | direct |
| monsters | 7 | ACID_SLIME_SMALL | AcidSlime_S | 2762 | direct |
| monsters | 8 | ACID_SLIME_MEDIUM | AcidSlime_M | 2266 | direct |
| monsters | 9 | SPIKE_SLIME_LARGE | SpikeSlime_L | 20 | direct |
| monsters | 10 | ACID_SLIME_LARGE | AcidSlime_L | 122 | direct |
| monsters | 11 | SLIME_BOSS | SlimeBoss | 0 | direct |
| monsters | 12 | GREMLIN_NOB | GremlinNob | 168 | direct |
| monsters | 13 | SENTRY | Sentry | 1521 | direct |
| monsters | 15 | LAGAVULIN | Lagavulin | 54580 | direct |
| monsters | 16 | GREMLIN_WARRIOR | GremlinWarrior | 0 | direct |
| monsters | 17 | GREMLIN_THIEF | GremlinThief | 0 | direct |
| monsters | 18 | GREMLIN_FAT | GremlinFat | 0 | direct |
| monsters | 19 | GREMLIN_TSUNDERE | GremlinTsundere | 0 | direct |
| monsters | 20 | GREMLIN_WIZARD | GremlinWizard | 0 | direct |
| monsters | 21 | THE_GUARDIAN | TheGuardian | 0 | direct |
| monsters | 22 | HEXAGHOST | Hexaghost | 21782 | direct |
| monsters | 23 | SLAVER_BLUE | SlaverBlue | 96 | direct |
| monsters | 24 | SLAVER_RED | SlaverRed | 32 | direct |
| monsters | 25 | FUNGI_BEAST | FungiBeast | 128 | direct |
| monsters | 26 | LOOTER | Looter | 27524 | direct |
| relics | 1 | BURNING_BLOOD | Burning Blood | 24042 | direct |
| relics | 2 | ANCHOR | Anchor | 16348 | direct |
| relics | 3 | BAG_OF_MARBLES | Bag of Marbles | 16380 | direct |
| relics | 4 | BAG_OF_PREPARATION | Bag of Preparation | 16171 | direct |
| relics | 5 | BLOOD_VIAL | Blood Vial | 16354 | direct |
| relics | 6 | BRONZE_SCALES | Bronze Scales | 16364 | direct |
| relics | 7 | CENTENNIAL_PUZZLE | Centennial Puzzle | 16296 | direct |
| relics | 8 | LANTERN | Lantern | 16473 | direct |
| relics | 9 | NUNCHAKU | Nunchaku | 16428 | direct |
| relics | 10 | ODDLY_SMOOTH_STONE | Oddly Smooth Stone | 16317 | direct |
| relics | 11 | ORICHALCUM | Orichalcum | 16337 | direct |
| relics | 12 | PEN_NIB | Pen Nib | 16418 | direct |
| relics | 13 | RED_SKULL | Red Skull | 16257 | direct |
| relics | 14 | VAJRA | Vajra | 16373 | direct |
| relics | 15 | HAPPY_FLOWER | Happy Flower | 16378 | direct |
| relics | 16 | AKABEKO | Akabeko | 16319 | direct |
| relics | 17 | ANCIENT_TEA_SET | Ancient Tea Set | 16369 | direct |
| relics | 18 | ART_OF_WAR | Art of War | 16361 | direct |
| relics | 19 | BOOT | Boot | 16285 | direct |
| relics | 20 | CERAMIC_FISH | CeramicFish | 16360 | direct |
| relics | 21 | DREAM_CATCHER | Dream Catcher | 16366 | direct |
| relics | 22 | JUZU_BRACELET | Juzu Bracelet | 16389 | direct |
| relics | 23 | MAW_BANK | MawBank | 16310 | direct |
| relics | 24 | MEAL_TICKET | MealTicket | 16240 | direct |
| relics | 25 | OMAMORI | Omamori | 16320 | direct |
| relics | 26 | POTION_BELT | Potion Belt | 16477 | direct |
| relics | 27 | PRESERVED_INSECT | PreservedInsect | 16371 | direct |
| relics | 28 | REGAL_PILLOW | Regal Pillow | 16273 | direct |
| relics | 29 | SMILING_MASK | Smiling Mask | 16356 | direct |
| relics | 30 | STRAWBERRY | Strawberry | 16361 | direct |
| relics | 31 | TINY_CHEST | Tiny Chest | 16322 | direct |
| relics | 32 | TOY_ORNITHOPTER | Toy Ornithopter | 16351 | direct |
| relics | 33 | WAR_PAINT | War Paint | 16316 | direct |
| relics | 34 | WHETSTONE | Whetstone | 16298 | direct |
| relics | 35 | CIRCLET | Circlet | 0 | direct |
| relics | 36 | BLUE_CANDLE | Blue Candle | 16260 | direct |
| relics | 37 | GREMLIN_HORN | Gremlin Horn | 16349 | direct |
| relics | 38 | HORN_CLEAT | HornCleat | 16255 | direct |
| relics | 39 | INK_BOTTLE | InkBottle | 16392 | direct |
| relics | 40 | KUNAI | Kunai | 16377 | direct |
| relics | 41 | LETTER_OPENER | Letter Opener | 16356 | direct |
| relics | 42 | MERCURY_HOURGLASS | Mercury Hourglass | 16341 | direct |
| relics | 43 | ORNAMENTAL_FAN | Ornamental Fan | 16385 | direct |
| relics | 44 | SHURIKEN | Shuriken | 16385 | direct |
| relics | 45 | SUNDIAL | Sundial | 16346 | direct |
| relics | 46 | SELF_FORMING_CLAY | Self Forming Clay | 16379 | direct |
| relics | 47 | PAPER_PHROG | Paper Frog | 16392 | direct |
| relics | 48 | STRIKE_DUMMY | StrikeDummy | 16373 | direct |
| relics | 49 | MEAT_ON_THE_BONE | Meat on the Bone | 16375 | direct |
| relics | 50 | MUMMIFIED_HAND | Mummified Hand | 16384 | direct |
| relics | 51 | PANTOGRAPH | Pantograph | 16355 | direct |
| relics | 52 | BOTTLED_FLAME | Bottled Flame | 16298 | direct |
| relics | 53 | BOTTLED_LIGHTNING | Bottled Lightning | 16328 | direct |
| relics | 54 | BOTTLED_TORNADO | Bottled Tornado | 16328 | direct |
| relics | 55 | DARKSTONE_PERIAPT | Darkstone Periapt | 16355 | direct |
| relics | 56 | ETERNAL_FEATHER | Eternal Feather | 16382 | direct |
| relics | 57 | FROZEN_EGG | Frozen Egg 2 | 16293 | direct |
| relics | 58 | MOLTEN_EGG | Molten Egg 2 | 16392 | direct |
| relics | 59 | TOXIC_EGG | Toxic Egg 2 | 16392 | direct |
| relics | 60 | PEAR | Pear | 16282 | direct |
| relics | 61 | QUESTION_CARD | Question Card | 16352 | direct |
| relics | 62 | SINGING_BOWL | Singing Bowl | 16392 | direct |
| relics | 63 | THE_COURIER | The Courier | 16392 | direct |
| relics | 64 | WHITE_BEAST_STATUE | White Beast Statue | 16362 | direct |
| relics | 65 | MATRYOSHKA | Matryoshka | 16378 | direct |
| relics | 66 | GINGER | Ginger | 16344 | direct |
| relics | 67 | OLD_COIN | Old Coin | 16365 | direct |
| relics | 68 | BIRD_FACED_URN | Bird Faced Urn | 16275 | direct |
| relics | 69 | UNCEASING_TOP | Unceasing Top | 16417 | direct |
| relics | 70 | TORII | Torii | 16382 | direct |
| relics | 71 | STONE_CALENDAR | StoneCalendar | 16356 | direct |
| relics | 72 | SHOVEL | Shovel | 16361 | direct |
| relics | 73 | WING_BOOTS | WingedGreaves | 16392 | direct |
| relics | 74 | THREAD_AND_NEEDLE | Thread and Needle | 16392 | direct |
| relics | 75 | TURNIP | Turnip | 16399 | direct |
| relics | 76 | ICE_CREAM | Ice Cream | 16320 | direct |
| relics | 77 | CALIPERS | Calipers | 16325 | direct |
| relics | 78 | LIZARD_TAIL | Lizard Tail | 16491 | direct |
| relics | 79 | PRAYER_WHEEL | Prayer Wheel | 16392 | direct |
| relics | 80 | GIRYA | Girya | 16384 | direct |
| relics | 81 | DEAD_BRANCH | Dead Branch | 16392 | direct |
| relics | 82 | DU_VU_DOLL | Du-Vu Doll | 16340 | direct |
| relics | 83 | POCKETWATCH | Pocketwatch | 16376 | direct |
| relics | 84 | MANGO | Mango | 16377 | direct |
| relics | 85 | INCENSE_BURNER | Incense Burner | 16358 | direct |
| relics | 86 | GAMBLING_CHIP | Gambling Chip | 16495 | direct |
| relics | 87 | PEACE_PIPE | Peace Pipe | 16392 | direct |
| relics | 88 | CAPTAINS_WHEEL | CaptainsWheel | 16392 | direct |
| relics | 89 | FOSSILIZED_HELIX | FossilizedHelix | 16376 | direct |
| relics | 90 | TUNGSTEN_ROD | TungstenRod | 16369 | direct |
| relics | 91 | MAGIC_FLOWER | Magic Flower | 16392 | direct |
| relics | 92 | CHARONS_ASHES | Charon's Ashes | 16369 | direct |
| relics | 93 | CHAMPION_BELT | Champion Belt | 16392 | direct |
| relics | 94 | SLING_OF_COURAGE | Sling | 16145 | direct |
| relics | 95 | HAND_DRILL | HandDrill | 16315 | direct |
| relics | 96 | TOOLBOX | Toolbox | 16304 | direct |
| relics | 97 | CHEMICAL_X | Chemical X | 16332 | direct |
| relics | 98 | LEES_WAFFLE | Lee's Waffle | 16254 | direct |
| relics | 99 | ORRERY | Orrery | 16310 | direct |
| relics | 100 | DOLLYS_MIRROR | DollysMirror | 16213 | direct |
| relics | 101 | ORANGE_PELLETS | OrangePellets | 16348 | direct |
| relics | 102 | PRISMATIC_SHARD | PrismaticShard | 16284 | direct |
| relics | 103 | CLOCKWORK_SOUVENIR | ClockworkSouvenir | 16247 | direct |
| relics | 104 | FROZEN_EYE | Frozen Eye | 16097 | direct |
| relics | 105 | THE_ABACUS | TheAbacus | 16304 | direct |
| relics | 106 | MEDICAL_KIT | Medical Kit | 16319 | direct |
| relics | 107 | CAULDRON | Cauldron | 16313 | direct |
| relics | 108 | STRANGE_SPOON | Strange Spoon | 16290 | direct |
| relics | 109 | MEMBERSHIP_CARD | Membership Card | 16206 | direct |
| relics | 110 | BRIMSTONE | Brimstone | 16335 | direct |
| relics | 111 | ODD_MUSHROOM | Odd Mushroom | 0 | direct |
| relics | 112 | FUSION_HAMMER | Fusion Hammer | 16779 | direct |
| relics | 113 | VELVET_CHOKER | Velvet Choker | 16479 | direct |
| relics | 114 | RUNIC_DOME | Runic Dome | 16733 | direct |
| relics | 115 | SLAVERS_COLLAR | SlaversCollar | 16392 | direct |
| relics | 116 | SNECKO_EYE | Snecko Eye | 16774 | direct |
| relics | 117 | PANDORAS_BOX | Pandora's Box | 16531 | direct |
| relics | 118 | CURSED_KEY | Cursed Key | 16496 | direct |
| relics | 119 | BUSTED_CROWN | Busted Crown | 16456 | direct |
| relics | 120 | ECTOPLASM | Ectoplasm | 16746 | direct |
| relics | 121 | TINY_HOUSE | Tiny House | 16437 | direct |
| relics | 122 | SOZU | Sozu | 16787 | direct |
| relics | 123 | PHILOSOPHERS_STONE | Philosopher's Stone | 16824 | direct |
| relics | 124 | ASTROLABE | Astrolabe | 16472 | direct |
| relics | 125 | BLACK_STAR | Black Star | 16650 | direct |
| relics | 126 | SACRED_BARK | SacredBark | 16392 | direct |
| relics | 127 | EMPTY_CAGE | Empty Cage | 16431 | direct |
| relics | 128 | RUNIC_PYRAMID | Runic Pyramid | 16677 | direct |
| relics | 129 | CALLING_BELL | Calling Bell | 16492 | direct |
| relics | 130 | COFFEE_DRIPPER | Coffee Dripper | 16517 | direct |
| relics | 131 | BLACK_BLOOD | Black Blood | 16166 | direct |
| relics | 132 | MARK_OF_PAIN | Mark of Pain | 16433 | direct |
| relics | 133 | RUNIC_CUBE | Runic Cube | 16445 | direct |
| relics | 134 | GOLDEN_IDOL | Golden Idol | 16544 | direct |
| relics | 135 | NEOWS_LAMENT | NeowsBlessing | 1264 | direct |
| relics | 136 | SPIRIT_POOP | Spirit Poop | 0 | direct |
| relics | 137 | WARPED_TONGS | WarpedTongs | 0 | direct |
| relics | 138 | CULTIST_MASK | CultistMask | 45 | direct |
| relics | 139 | FACE_OF_CLERIC | FaceOfCleric | 0 | direct |
| relics | 140 | GREMLIN_MASK | GremlinMask | 34 | direct |
| relics | 141 | NLOTHS_MASK | NlothsMask | 0 | direct |
| relics | 142 | SSSERPENT_HEAD | SsserpentHead | 0 | direct |
| potions | 1 | BLOOD_POTION | BloodPotion | 42 | direct |
| potions | 2 | ELIXIR | ElixirPotion | 26 | direct |
| potions | 3 | HEART_OF_IRON | HeartOfIron | 17 | direct |
| potions | 4 | BLOCK_POTION | Block Potion | 122 | direct |
| potions | 5 | DEXTERITY_POTION | Dexterity Potion | 82 | direct |
| potions | 6 | ENERGY_POTION | Energy Potion | 162 | direct |
| potions | 7 | EXPLOSIVE_POTION | Explosive Potion | 100 | direct |
| potions | 8 | FIRE_POTION | Fire Potion | 98 | direct |
| potions | 9 | STRENGTH_POTION | Strength Potion | 46 | direct |
| potions | 10 | SWIFT_POTION | Swift Potion | 114 | direct |
| potions | 11 | WEAK_POTION | Weak Potion | 162 | direct |
| potions | 12 | FEAR_POTION | FearPotion | 30 | direct |
| potions | 13 | ATTACK_POTION | AttackPotion | 63 | direct |
| potions | 14 | SKILL_POTION | SkillPotion | 67 | direct |
| potions | 15 | POWER_POTION | PowerPotion | 58 | direct |
| potions | 16 | COLORLESS_POTION | ColorlessPotion | 58 | direct |
| potions | 17 | STEROID_POTION | SteroidPotion | 34 | direct |
| potions | 18 | SPEED_POTION | SpeedPotion | 52 | direct |
| potions | 19 | BLESSING_OF_THE_FORGE | BlessingOfTheForge | 39 | direct |
| potions | 20 | REGEN_POTION | Regen Potion | 94 | direct |
| potions | 21 | ANCIENT_POTION | Ancient Potion | 122 | direct |
| potions | 22 | LIQUID_BRONZE | LiquidBronze | 19 | direct |
| potions | 23 | GAMBLERS_BREW | GamblersBrew | 42 | direct |
| potions | 24 | ESSENCE_OF_STEEL | EssenceOfSteel | 32 | direct |
| potions | 25 | DUPLICATION_POTION | DuplicationPotion | 24 | direct |
| potions | 26 | DISTILLED_CHAOS | DistilledChaos | 58 | direct |
| potions | 27 | LIQUID_MEMORIES | LiquidMemories | 56 | direct |
| potions | 28 | CULTIST_POTION | CultistPotion | 19 | direct |
| potions | 29 | FRUIT_JUICE | Fruit Juice | 48 | direct |
| potions | 30 | SNECKO_OIL | SneckoOil | 37 | direct |
| potions | 31 | FAIRY_POTION | FairyPotion | 47 | direct |
| potions | 32 | SMOKE_BOMB | SmokeBomb | 17 | direct |
| potions | 33 | ENTROPIC_BREW | EntropicBrew | 20 | direct |
| events | 1 | BIG_FISH | Big Fish | 15873 | direct |
| events | 2 | THE_CLERIC | The Cleric | 15906 | direct |
| events | 3 | DEAD_ADVENTURER | Dead Adventurer | 16392 | direct |
| events | 4 | GOLDEN_IDOL | Golden Idol | 16544 | direct |
| events | 5 | GOLDEN_WING | Golden Wing | 16054 | direct |
| events | 6 | WORLD_OF_GOOP | World of Goop | 15834 | direct |
| events | 7 | LIARS_GAME | Liars Game | 16004 | direct |
| events | 8 | LIVING_WALL | Living Wall | 15787 | direct |
| events | 9 | MUSHROOMS | Mushrooms | 16392 | direct |
| events | 10 | SCRAP_OOZE | Scrap Ooze | 16010 | direct |
| events | 11 | SHINING_LIGHT | Shining Light | 16143 | direct |
| events | 12 | MATCH_AND_KEEP | Match and Keep! | 16277 | direct |
| events | 13 | GOLDEN_SHRINE | Golden Shrine | 16312 | direct |
| events | 14 | TRANSMORGRIFIER | Transmorgrifier | 16349 | direct |
| events | 15 | PURIFIER | Purifier | 16312 | direct |
| events | 16 | UPGRADE_SHRINE | Upgrade Shrine | 16315 | direct |
| events | 17 | WHEEL_OF_CHANGE | Wheel of Change | 16281 | direct |
| events | 18 | ACCURSED_BLACKSMITH | Accursed Blacksmith | 16293 | direct |
| events | 19 | BONFIRE_ELEMENTALS | Bonfire Elementals | 16363 | direct |
| events | 20 | DESIGNER | Designer | 16392 | direct |
| events | 21 | DUPLICATOR | Duplicator | 16392 | direct |
| events | 22 | FACE_TRADER | FaceTrader | 16290 | direct |
| events | 23 | FOUNTAIN_OF_CLEANSING | Fountain of Cleansing | 16392 | direct |
| events | 24 | KNOWING_SKULL | Knowing Skull | 16392 | direct |
| events | 25 | LAB | Lab | 16345 | direct |
| events | 26 | NLOTH | N'loth | 16392 | direct |
| events | 27 | NOTE_FOR_YOURSELF | NoteForYourself | 0 | direct |
| events | 28 | SECRET_PORTAL | SecretPortal | 16392 | direct |
| events | 29 | THE_JOUST | The Joust | 16392 | direct |
| events | 30 | WE_MEET_AGAIN | WeMeetAgain | 16235 | direct |
| events | 31 | THE_WOMAN_IN_BLUE | The Woman in Blue | 16357 | direct |
| encounters | 1 | CULTIST | Cultist | 16990 | direct |
| encounters | 2 | JAW_WORM_ENC | Jaw Worm | 13722 | direct |
| encounters | 3 | TWO_LOUSE | 2 Louse | 8259 | direct |
| encounters | 4 | SMALL_SLIMES | Small Slimes | 9011 | direct |
| encounters | 5 | BLUE_SLAVER | Blue Slaver | 26931 | direct |
| encounters | 6 | GREMLIN_GANG | Gremlin Gang | 13064 | direct |
| encounters | 7 | LOOTER | Looter | 27524 | direct |
| encounters | 8 | LARGE_SLIME | Large Slime | 24751 | direct |
| encounters | 9 | LOTS_OF_SLIMES | Lots of Slimes | 14650 | direct |
| encounters | 10 | EXORDIUM_THUGS | Exordium Thugs | 19159 | direct |
| encounters | 11 | EXORDIUM_WILDLIFE | Exordium Wildlife | 21174 | direct |
| encounters | 12 | RED_SLAVER | Red Slaver | 14404 | direct |
| encounters | 13 | THREE_LOUSE | 3 Louse | 24977 | direct |
| encounters | 14 | TWO_FUNGI_BEASTS | 2 Fungi Beasts | 26464 | direct |
| encounters | 15 | GREMLIN_NOB | Gremlin Nob | 54993 | direct |
| encounters | 16 | LAGAVULIN_ENC | Lagavulin | 54580 | direct |
| encounters | 17 | THREE_SENTRIES | 3 Sentries | 54865 | direct |
| encounters | 18 | THE_GUARDIAN | The Guardian | 21395 | direct |
| encounters | 19 | HEXAGHOST | Hexaghost | 21782 | direct |
| encounters | 20 | SLIME_BOSS | Slime Boss | 22391 | direct |
| encounters | 21 | THE_MUSHROOM_LAIR | The Mushroom Lair | 0 | direct |
| a20 | 1 | A1 | n/a | n/a | sweep |
| a20 | 2 | A2 | n/a | n/a | sweep |
| a20 | 3 | A3 | n/a | n/a | sweep |
| a20 | 4 | A4 | n/a | n/a | sweep |
| a20 | 5 | A5 | n/a | n/a | sweep |
| a20 | 6 | A6 | n/a | n/a | sweep |
| a20 | 7 | A7 | n/a | n/a | sweep |
| a20 | 8 | A8 | n/a | n/a | sweep |
| a20 | 9 | A9 | n/a | n/a | sweep |
| a20 | 10 | A10 | n/a | n/a | sweep |
| a20 | 11 | A11 | n/a | n/a | sweep |
| a20 | 12 | A12 | n/a | n/a | sweep |
| a20 | 13 | A13 | n/a | n/a | sweep |
| a20 | 14 | A14 | n/a | n/a | sweep |
| a20 | 15 | A15 | n/a | n/a | sweep |
| a20 | 16 | A16 | n/a | n/a | sweep |
| a20 | 17 | A17 | n/a | n/a | sweep |
| a20 | 18 | A18 | n/a | n/a | sweep |
| a20 | 19 | A19 | n/a | n/a | sweep |
| a20 | 20 | A20 | n/a | n/a | sweep |
