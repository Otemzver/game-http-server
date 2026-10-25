#include "collision_detector.h"
#include <cassert>

namespace collision_detector {

CollectionResult TryCollectPoint(geom::Point2D a, geom::Point2D b, geom::Point2D c) {
    // Проверим, что перемещение ненулевое.
    // Тут приходится использовать строгое равенство, а не приближённое,
    // пскольку при сборе заказов придётся учитывать перемещение даже на небольшое
    // расстояние.
    assert(b.x != a.x || b.y != a.y);
    const double u_x = c.x - a.x;
    const double u_y = c.y - a.y;
    const double v_x = b.x - a.x;
    const double v_y = b.y - a.y;
    const double u_dot_v = u_x * v_x + u_y * v_y;
    const double u_len2 = u_x * u_x + u_y * u_y;
    const double v_len2 = v_x * v_x + v_y * v_y;
    const double proj_ratio = u_dot_v / v_len2;
    const double sq_distance = u_len2 - (u_dot_v * u_dot_v) / v_len2;

    return CollectionResult(sq_distance, proj_ratio);
}

std::vector<GatheringEvent> FindGatherEvents(const ItemGathererProvider& provider) {
    std::vector<GatheringEvent> events;
    events.reserve(provider.ItemsCount() * provider.GatherersCount());

    for (size_t gatherer_id = 0; gatherer_id < provider.GatherersCount(); ++gatherer_id) {
        const Gatherer gatherer = provider.GetGatherer(gatherer_id);

        // A stationary gatherer creates no collision events. The comparison is
        // intentionally exact: any non-zero movement must be processed.
        if (gatherer.start_pos.x == gatherer.end_pos.x &&
            gatherer.start_pos.y == gatherer.end_pos.y) {
            continue;
        }

        for (size_t item_id = 0; item_id < provider.ItemsCount(); ++item_id) {
            const Item item = provider.GetItem(item_id);
            const CollectionResult result = TryCollectPoint(
                gatherer.start_pos, gatherer.end_pos, item.position);

            if (result.IsCollected(gatherer.width + item.width)) {
                events.push_back({item_id, gatherer_id,
                                  result.sq_distance, result.proj_ratio});
            }
        }
    }

    std::sort(events.begin(), events.end(),
              [](const GatheringEvent& lhs, const GatheringEvent& rhs) {
                  return lhs.time < rhs.time;
              });
    return events;
}

}  // namespace collision_detector
