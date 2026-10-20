#include <dwe/scene/world.hpp>

namespace dwe::scene {

void World::destroy(Entity entity) {
    if (registry_.valid(entity)) {
        registry_.destroy(entity);
    }
}

} // namespace dwe::scene
