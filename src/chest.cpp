static std::vector<uint64_t> g_chest_classes;   // resolved once per attach

static void build_class_cache() {
    g_chest_classes.clear();
    uint64_t world = ue::gworld();
    if (!world) return;
    uint64_t level = mem::read<uint64_t>(world + g_cfg.UWorld_PersistentLevel, 0);
    if (!level) return;
    uint64_t arr = mem::read<uint64_t>(level + g_cfg.ULevel_Actors, 0);
    int32_t  n   = mem::read<int32_t>(level + g_cfg.ULevel_Actors + 8, 0);
    if (!arr || n <= 0) return;

    auto ptrs = mem::read_bytes(arr, (size_t)n * 8);   // ONE bulk read
    if (ptrs.size() != (size_t)n * 8) return;

    std::unordered_set<uint64_t> seen;
    for (int32_t i = 0; i < n; ++i) {
        uint64_t a = *(uint64_t*)(ptrs.data() + i * 8);
        if (!a) continue;
        uint64_t cls = mem::read<uint64_t>(a + g_cfg.UObject_Class, 0);
        if (!cls || !seen.insert(cls).second) continue;
        if (is_chest_name(ue::name_of(cls))) g_chest_classes.push_back(cls);
    }
}

std::vector<Chest> chest::scan(const Vec3& origin, float radius) {
    std::vector<Chest> out;
    if (g_chest_classes.empty()) build_class_cache();

    uint64_t world = ue::gworld();
    if (!world) return out;
    uint64_t level = mem::read<uint64_t>(world + g_cfg.UWorld_PersistentLevel, 0);
    if (!level) return out;
    uint64_t arr = mem::read<uint64_t>(level + g_cfg.ULevel_Actors, 0);
    int32_t  n   = mem::read<int32_t>(level + g_cfg.ULevel_Actors + 8, 0);
    if (!arr || n <= 0) return out;

    auto ptrs = mem::read_bytes(arr, (size_t)n * 8);   // ONE bulk read, no per-actor pointer chase
    if (ptrs.size() != (size_t)n * 8) return out;

    const double r2 = (double)radius * radius;
    for (int32_t i = 0; i < n; ++i) {
        uint64_t a = *(uint64_t*)(ptrs.data() + i * 8);
        if (!a) continue;

        uint64_t cls = mem::read<uint64_t>(a + g_cfg.UObject_Class, 0);
        if (std::find(g_chest_classes.begin(), g_chest_classes.end(), cls) == g_chest_classes.end())
            continue;

        Vec3 loc = ue::location_of(a);
        if (!loc.x && !loc.y && !loc.z) continue;
        double d2 = (loc - origin).len();
        if (d2 > r2) continue;

        out.push_back({ a, loc, (float)std::sqrt(d2), already_searched(a, "") });
    }
    std::sort(out.begin(), out.end(), [](const Chest& x, const Chest& y){ return x.dist < y.dist; });
    return out;
}