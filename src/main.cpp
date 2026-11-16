#include "humanize.hpp"

static bool run_one_chest(const Chest& c) {
    if (!mem::begin_session()) return false;

    uint64_t pawn  = ue::local_pawn();
    uint64_t mover = tp::resolve_mover(pawn);
    if (!mover) { mem::end_session(); return false; }

    Vec3 dest = c.loc;
    dest.z += 30.0;

    tp::go_to(mover, dest, human::step_variance(g_cfg.max_step_units), 3);
    for (int i = 0; i < human::between(2, 5); ++i) { tp::hold(mover, dest); Sleep(human::between(15, 35)); }

    if (g_cfg.aim_at_chest) ue::look_at(ue::local_controller(), c.loc);
    if (input::game_focused()) input::tap(g_cfg.interact_key, human::between(35, 70));

    Sleep(human::between(g_cfg.settle_ms, g_cfg.settle_ms * 2));
    tp::hold(mover, dest);

    mem::end_session();          // handle dies here
    g_visited.insert(c.actor);
    return true;
}

// main loop: rescan only when the list runs dry, random pick among the nearest few
static std::vector<Chest> g_pool;
static int g_since_pause = 0;

...
    if (g_pool.empty()) { g_pool = chest::scan(loc, g_cfg.search_radius); }
    Chest* next = nullptr;
    size_t window = std::min<size_t>(g_pool.size(), 3);
    if (window) {
        size_t pick = human::between<size_t>(0, window - 1);
        next = &g_pool[pick];
        g_pool.erase(g_pool.begin() + pick);
    }
    if (!next) { Sleep(human::between(120, 260)); continue; }

    if (++g_since_pause >= human::between(8, 12)) {
        g_since_pause = 0;
        Sleep(human::between(2500, 8000));      // "looking at my phone" pause
    }
    if (!run_one_chest(*next)) Sleep(150);