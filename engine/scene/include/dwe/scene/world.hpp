#pragma once

#include <entt/entity/registry.hpp>

#include <optional>

namespace dwe::scene {

using Entity = entt::entity;

// The entities of the world and their components (ADR 0012): a thin owner of an EnTT registry. Systems add and read
// components through registry(); the helpers cover what every user needs.
class World {
public:
    // Creates an entity without components.
    Entity create() { return registry_.create(); }

    // Destroys an entity and its components. Does nothing for an entity that is not alive.
    void destroy(Entity entity);

    // Whether the entity exists.
    [[nodiscard]] bool alive(Entity entity) const { return registry_.valid(entity); }

    // The first entity with a component (a tag such as PlayerTag), or nothing.
    template <typename Component>
    [[nodiscard]] std::optional<Entity> first() const {
        const auto view = registry_.view<const Component>();
        if (view.begin() == view.end()) {
            return std::nullopt;
        }
        return *view.begin();
    }

    [[nodiscard]] entt::registry& registry() { return registry_; }
    [[nodiscard]] const entt::registry& registry() const { return registry_; }

private:
    entt::registry registry_;
};

} // namespace dwe::scene
