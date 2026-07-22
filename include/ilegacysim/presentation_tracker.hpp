#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

#include "ilegacysim/surface_store.hpp"

namespace ilegacysim {

// Host-backend-independent description of a surface placement in one completed
// display transaction. OS-specific adapters translate their native transaction
// formats into this stable representation.
struct PresentationRectangle {
  float x{};
  float y{};
  float width{};
  float height{};
};

struct PresentationLayer {
  std::uint32_t order{};
  std::uint32_t surface_id{};
  SurfaceStore::Provenance surface_provenance;
  PresentationRectangle source;
  PresentationRectangle destination;
  std::uint32_t flags{};
};

struct PresentationFrame {
  std::uint64_t sequence{};
  std::uint32_t submitting_process_id{};
  std::vector<PresentationLayer> layers;
};

// Initially observational: recording a frame never controls rendering or input.
// Future consumers can use the snapshot without coupling the common scene model
// to LayerKit, MobileFramebuffer, QuartzCore, IOSurface, or SDL.
class PresentationTracker {
public:
  [[nodiscard]] std::uint64_t
  record(std::uint32_t submitting_process_id,
         std::vector<PresentationLayer> layers);
  [[nodiscard]] std::optional<PresentationFrame> latest() const;

private:
  mutable std::mutex mutex_;
  std::uint64_t next_sequence_{1};
  std::optional<PresentationFrame> latest_;
};

} // namespace ilegacysim
