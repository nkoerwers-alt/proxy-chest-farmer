bool tp::go_to(uint64_t mover, const Vec3& target, float max_step, int delay_ms) {
    if (!mover) return false;
    Vec3 cur = mem::read<Vec3>(mover + g_cfg.USceneComponent_ComponentLocation, {});
    double dist = std::sqrt((target - cur).len());
    if (dist < 1.0) return true;

    int steps = (int)std::ceil(dist / std::max(max_step, 1.f));
    steps = std::clamp(steps, 1, 120);

    for (int i = 1; i <= steps; ++i) {
        Vec3 p = cur.lerp(target, (double)i / steps);
        mem::write<Vec3>(mover + g_cfg.USceneComponent_ComponentLocation, p);
        if (delay_ms > 0) Sleep(delay_ms);
    }
    return true;
}

// only write if we actually drifted -- no more 4-writes-per-20ms spam
void tp::hold(uint64_t mover, const Vec3& target) {
    if (!mover) return;
    Vec3 cur = mem::read<Vec3>(mover + g_cfg.USceneComponent_ComponentLocation, {});
    if (std::sqrt((target - cur).len()) > 1.0)
        mem::write<Vec3>(mover + g_cfg.USceneComponent_ComponentLocation, target);
}