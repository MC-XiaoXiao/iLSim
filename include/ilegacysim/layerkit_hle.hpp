#pragma once

#include <memory>

#include "ilegacysim/layerkit_compatibility.hpp"

namespace ilegacysim {

struct KernelSharedState;
class Output;
class UserlandHleRegistry;

// Owns the firmware-facing LayerKit hooks and their cross-command scene
// compatibility state. CompatibilityKernel only controls lifecycle; command
// decoding, window placement, and detached-root repair remain in this module.
class LayerKitHle {
public:
  void register_handlers(UserlandHleRegistry &registry,
                         std::shared_ptr<KernelSharedState> shared_state,
                         Output &output);
  void set_shared_state(std::shared_ptr<KernelSharedState> shared_state);
  void reset();
  void inherit_state(const LayerKitHle &parent);

private:
  LayerKitRootCompatibility compatibility_;
  std::shared_ptr<KernelSharedState> shared_state_;
};

} // namespace ilegacysim
