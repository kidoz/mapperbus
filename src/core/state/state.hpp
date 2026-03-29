#pragma once

namespace mapperbus::core {

/// Marker interface for save/load state serialization.
/// StateWriter / StateReader types are deferred to a future phase.
class Serializable {
  public:
    virtual ~Serializable() = default;
    virtual void save_state(/* StateWriter& */) const {}
    virtual void load_state(/* StateReader& */) {}
};

} // namespace mapperbus::core
