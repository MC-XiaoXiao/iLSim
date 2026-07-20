#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>

namespace ilegacysim {

class DisplayState;
class UserlandHleCall;
class UserlandHleRegistry;
class SurfaceStore;

class MobileFramebufferHle {
public:
    MobileFramebufferHle(UserlandHleRegistry& registry,
                         std::shared_ptr<DisplayState> display,
                         std::shared_ptr<SurfaceStore> surfaces = {});

    void reset();
    void inherit_state(const MobileFramebufferHle& parent);
    void set_display(std::shared_ptr<DisplayState> display);
    [[nodiscard]] bool has_active_layers() const;

private:
    void set_background_color(UserlandHleCall& call);
    void set_layer(UserlandHleCall& call);
    void submit_layers(UserlandHleCall& call);

    struct Rectangle {
        float x{};
        float y{};
        float width{};
        float height{};
    };
    struct LayerState {
        std::uint32_t surface_id{};
        Rectangle source;
        Rectangle destination;
        std::uint32_t flags{};
    };

    std::shared_ptr<DisplayState> display_;
    std::shared_ptr<SurfaceStore> surface_store_;
    std::map<std::uint32_t, LayerState> layers_;
    std::uint32_t next_swap_id_{1};
    std::uint32_t background_argb_{0xff000000U};
};

}  // namespace ilegacysim
