#include "ilegacysim/fig_movie_hle.hpp"

#include <cstdint>
#include <string>
#include <string_view>

#include "ilegacysim/userland_hle.hpp"

namespace ilegacysim {
namespace {

constexpr std::string_view celestial_image{
    "/System/Library/Frameworks/Celestial.framework/Celestial"};
constexpr std::string_view springboard_image{
    "/System/Library/CoreServices/SpringBoard.app/SpringBoard"};
constexpr std::uint32_t media_server_unavailable = 5;

}  // namespace

void register_fig_movie_hle(UserlandHleRegistry& registry) {
    registry.register_function(
        std::string{celestial_image}, "_FigMovieClient_Register",
        [](UserlandHleCall& call) {
            if (!call.image_loaded(springboard_image)) {
                call.resume_original();
                return;
            }

            // The iPhoneOS 1.0 MIG client returns two values through arguments
            // 6 and 7. Clear them and report an unavailable server so the AV
            // controller does not wait for a connection notification.
            const auto callback_port_output = call.argument(6);
            const auto server_token_output = call.argument(7);
            if (!call.write32(callback_port_output, 0) ||
                !call.write32(server_token_output, 0)) {
                call.set_return(media_server_unavailable);
                return;
            }
            call.set_return(media_server_unavailable);
        });
}

}  // namespace ilegacysim
