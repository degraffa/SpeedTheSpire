#pragma once

// Rest-site menu and effects.
//
// The menu is transient screen state, so it belongs to RunController rather
// than the save-parity RunState.  Its contents are derived from the master deck
// and acquisition-ordered relic list every time legal actions are requested.
// This mirrors CampfireUI.initializeButtons: Rest, Smith, then each relic's
// addCampfireOption in relic acquisition order.
//
// Provenance (methods read in full from the decompiled game -- 12-18-2022,
// design 1.2; the tree these line numbers resolve against is unchanged, only
// its mislabelling was corrected, design 11 v0.1.7):
//   * CampfireUI.initializeButtons (CampfireUI.java:81-107)
//   * RestOption.<init> / CampfireSleepEffect.<init> / update
//     (RestOption.java:25-39; CampfireSleepEffect.java:38-82)
//   * SmithOption.useOption / CampfireSmithEffect.update
//     (SmithOption.java:31-36; CampfireSmithEffect.java:42-72)
//   * Girya.addCampfireOption / CampfireLiftEffect.update
//     (Girya.java:60-62; CampfireLiftEffect.java:35-58)
//   * PeacePipe.addCampfireOption / CampfireTokeEffect.update
//     (PeacePipe.java:47-49; CampfireTokeEffect.java:40-67)
//   * Shovel.addCampfireOption / CampfireDigEffect.update
//     (Shovel.java:46-48; CampfireDigEffect.java:35-55)
//   * CardGroup.getUpgradableCards / getPurgeableCards /
//     getGroupWithoutBottledCards (CardGroup.java:961-969, 978-986,
//     1084-1092)

#include <cstdint>
#include <type_traits>

#include "sts/engine/relic_pools.hpp"
#include "sts/engine/run_state.hpp"

namespace sts::engine {

enum class RestScreen : uint8_t {
    NONE = 0,
    MENU = 1,
    SMITH = 2,
    TOKE = 3,
    DREAM_CATCHER = 4,
};

enum class RestOptionKind : uint8_t {
    REST = 0,
    SMITH = 1,
    LIFT = 2,
    TOKE = 3,
    DIG = 4,
    // The Ruby Key button (RecallOption.java; CampfireUI.java:94-96): appended
    // AFTER the veto sweep whenever the final act is available and the run's
    // Ruby Key is untaken, always usable. Pressing it obtains the RED key and
    // completes the room -- the campfire action is spent
    // (RecallOption.useOption -> CampfireRecallEffect.java:39-53 ->
    // ObtainKeyEffect: Settings.hasRubyKey = true).
    RECALL = 5,
};

// Settings.isFinalActAvailable (Settings.java:642): the AND of all three
// characters' profile _WIN prefs -- PROFILE-derived and constant for a whole
// run. The frozen capture profile has it TRUE (every captured rest site lists
// the Recall button third), so the engine carries it as a profile constant on
// the same footing as the skeleton's fixed A20 (kMonsterAscension). A profile
// without the final act simply never appends the button.
inline constexpr bool kFinalActAvailable = true;

// Rest + Smith + at most one option from each owned relic, + Recall.
inline constexpr int kRestOptionCap = 2 + kRelicCap + 1;

struct RestOptionEntry {
    uint8_t kind;         // RestOptionKind
    uint8_t relic_index;  // source relic for LIFT/TOKE/DIG; 0xFF otherwise
    bool usable;
    uint8_t pad;
};

static_assert(std::is_trivially_copyable_v<RestOptionEntry>);
static_assert(sizeof(RestOptionEntry) == 4);

struct RestMenu {
    RestOptionEntry entries[kRestOptionCap];
    uint8_t count;
    uint8_t pad[3];
};

static_assert(std::is_trivially_copyable_v<RestMenu>);

// The only rest-site state that is not derivable from RunState is which menu,
// grid, or Dream Catcher card screen is currently open.
struct RestSiteState {
    uint8_t screen;  // RestScreen
    uint8_t pad[3];
};

static_assert(std::is_trivially_copyable_v<RestSiteState>);
static_assert(sizeof(RestSiteState) == 4);

// Java casts maxHealth * 0.3f directly to int, i.e. truncates toward zero,
// then adds Regal Pillow's 15 when owned.  max HP is positive, so the cast is
// floor.  There is deliberately no ascension branch.
[[nodiscard]] int rest_heal_amount(const RunState& rs) noexcept;

[[nodiscard]] bool rest_card_upgradeable(const CardInstance& card) noexcept;
[[nodiscard]] bool rest_card_purgeable(const CardInstance& card) noexcept;

// Rebuild CampfireUI's button list in exact insertion order, then apply the
// canUseCampfireOption veto sweep over the whole list (CampfireUI
// .initializeButtons, CampfireUI.java:81-93). Fusion Hammer clears Smith and
// Coffee Dripper clears Rest; the relics' own updateUsability calls are
// cosmetic and are NOT the disable (see the derivation at the definition).
[[nodiscard]] RestMenu build_rest_menu(const RunState& rs) noexcept;

// Is any button usable? CampfireUI.java:97-104 asks exactly this after building
// the list, and when the answer is no it sets the room's phase to COMPLETE with
// waitTimer 0 -- the campfire is skipped with no player decision at all. The run
// layer applies that at rest-room entry; this is the predicate it applies.
[[nodiscard]] bool rest_menu_has_usable_option(const RestMenu& menu) noexcept;

// Effects return false without mutation when the chosen row is illegal.
[[nodiscard]] bool rest_apply_heal(RunState& rs) noexcept;
[[nodiscard]] bool rest_upgrade_card(RunState& rs, uint16_t deck_index) noexcept;
[[nodiscard]] bool rest_purge_card(RunState& rs, uint16_t deck_index) noexcept;
[[nodiscard]] bool rest_lift(RunState& rs, uint8_t relic_index) noexcept;

// CampfireDigEffect: one relicRng tier roll followed by the selected pool's
// front pop with the ordinary canSpawn recheck.  The caller puts the returned
// relic on the reward screen; acquisition happens only when it is claimed.
[[nodiscard]] RelicId rest_dig_relic(RunState& rs) noexcept;

}  // namespace sts::engine
