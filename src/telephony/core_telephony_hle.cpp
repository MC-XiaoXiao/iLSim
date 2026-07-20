#include "ilegacysim/core_telephony_hle.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "ilegacysim/address_space.hpp"
#include "ilegacysim/userland_hle.hpp"

namespace ilegacysim {
namespace {

constexpr std::string_view core_telephony_image{
    "/System/Library/Frameworks/CoreTelephony.framework/CoreTelephony"};
constexpr std::string_view springboard_image{
    "/System/Library/CoreServices/SpringBoard.app/SpringBoard"};
constexpr std::string_view application_directory{"Applications/"};
// Fixed __nl_symbol_ptr slot in the iPhoneOS 1.0 SpringBoard executable. It
// points at CoreTelephony's exported registration-status variable, whose first
// word is an immortal CFString suitable for use as an opaque connection token.
constexpr std::uint32_t springboard_registration_status_import{0x00087430U};
constexpr std::uint32_t springboard_telephony_checked_in_method{0x0002a9f4U};
constexpr std::string_view offline_sim_status_export{
    "_kCTSIMSupportSIMStatusNotInserted"};

// These APIs have scalar return values in iPhoneOS 1.0. An offline handset
// has no data attachment, active data context, signal, airplane mode, or calls.
constexpr std::array<std::string_view, 5> offline_scalar_queries{
    "_CTRegistrationGetDataAttached",
    "_CTRegistrationGetDataContextActive",
    "_CTGetSignalStrength",
    "_CTPowerGetAirplaneMode",
    "_CTGetCurrentCallCount",
};

constexpr std::array<std::string_view, 3> observer_operations{
    "_CTTelephonyCenterAddObserver",
    "_CTTelephonyCenterRemoveObserver",
    "_CTTelephonyCenterRemoveEveryObserver",
};

bool is_offline_ui_client(const UserlandHleCall& call) {
    return call.image_loaded(springboard_image) ||
           call.image_loaded_beneath(application_directory);
}

std::uint32_t exported_object(UserlandHleCall& call,
                              std::string_view variable) {
    const auto address = call.symbol_address(variable);
    return address ? call.memory().read32(*address).value_or(0) : 0;
}

void return_firmware_object(UserlandHleCall& call,
                            std::string_view variable) {
    if (!is_offline_ui_client(call)) {
        call.resume_original();
        return;
    }
    call.set_return(exported_object(call, variable));
}

void return_empty_server_string(UserlandHleCall& call) {
    if (!is_offline_ui_client(call)) {
        call.resume_original();
        return;
    }
    const auto result = call.argument(0);
    const auto value_output = call.argument(2);
    if (result == 0 || value_output == 0 ||
        !call.write32(result, 0) || !call.write32(result + 4U, 0) ||
        !call.write32(value_output, 0)) {
        call.set_return(0);
        return;
    }
    call.set_return(result);
}

std::uint32_t springboard_imported_object(UserlandHleCall& call,
                                          std::uint32_t import_address) {
    const auto exported_variable = call.memory().read32(import_address);
    return exported_variable
               ? call.memory().read32(*exported_variable).value_or(0)
               : 0;
}

void return_server_value(UserlandHleCall& call, std::uint32_t value) {
    if (!is_offline_ui_client(call)) {
        call.resume_original();
        return;
    }
    const auto result = call.argument(0);
    const auto value_output = call.argument(2);
    if (result == 0 || value_output == 0 ||
        !call.write32(result, 0) || !call.write32(result + 4U, 0) ||
        !call.write32(value_output, value)) {
        call.set_return(0);
        return;
    }
    call.set_return(result);
}

void return_server_success(UserlandHleCall& call) {
    if (!is_offline_ui_client(call)) {
        call.resume_original();
        return;
    }
    const auto result = call.argument(0);
    if (result == 0 || !call.write32(result, 0) ||
        !call.write32(result + 4U, 0)) {
        call.set_return(0);
        return;
    }
    call.set_return(result);
}

}  // namespace

void register_core_telephony_hle(UserlandHleRegistry& registry) {
    // Every stock UI process shares one offline compatibility backend. Match
    // the application directory instead of maintaining a bundle whitelist,
    // while allowing CommCenter and other system daemons to keep their native
    // internal object lifecycles.
    // Without CommCenter, the stock method would keep the status controller in
    // its pre-check-in state and pass nil into SBStatusBarNoServiceView. Report
    // the already-implemented offline CoreTelephony boundary as checked in so
    // SpringBoard chooses and localizes its own NO_SERVICE/SEARCHING string.
    registry.register_address(
        std::string{springboard_image},
        springboard_telephony_checked_in_method,
        "-[SBStatusBarController telephonyControllerCheckedIn]",
        [](UserlandHleCall& call) { call.set_return(1); });
    for (const auto symbol : offline_scalar_queries) {
        registry.register_function(
            std::string{core_telephony_image}, std::string{symbol},
            [](UserlandHleCall& call) {
                if (is_offline_ui_client(call)) {
                    call.set_return(0);
                } else {
                    call.resume_original();
                }
            });
    }

    registry.register_function(
        std::string{core_telephony_image}, "_CTRegistrationGetStatus",
        [](UserlandHleCall& call) {
            return_firmware_object(
                call, "_kCTRegistrationStatusNotRegistered");
        });
    registry.register_function(
        std::string{core_telephony_image}, "_CTSIMSupportGetSIMStatus",
        [](UserlandHleCall& call) {
            return_firmware_object(call, offline_sim_status_export);
        });
    registry.register_function(
        std::string{core_telephony_image},
        "__CTServerConnectionCopySIMIdentity",
        [](UserlandHleCall& call) { return_empty_server_string(call); });
    registry.register_function(
        std::string{core_telephony_image},
        "__CTServerConnectionCopyMobileIdentity",
        [](UserlandHleCall& call) { return_empty_server_string(call); });
    registry.register_function(
        std::string{core_telephony_image},
        "__CTServerConnectionGetMobileSubscriberCountryCode",
        [](UserlandHleCall& call) { return_empty_server_string(call); });
    registry.register_function(
        std::string{core_telephony_image},
        "__CTServerConnectionGetSIMStatus",
        [](UserlandHleCall& call) {
            return_server_value(
                call, exported_object(call, offline_sim_status_export));
        });

    // Legacy clients gate their __CTServerConnection queries on a non-null
    // connection. Use an immortal CoreTelephony CFString as the opaque token:
    // the server HLEs never dereference it and retain/release remains valid.
    // SpringBoard imports the variable through its fixed prebound slot; other
    // applications resolve the same exported framework object directly.
    registry.register_function(
        std::string{core_telephony_image},
        "__CTServerConnectionCreate",
        [](UserlandHleCall& call) {
            if (!is_offline_ui_client(call)) {
                call.resume_original();
                return;
            }
            const auto token = call.image_loaded(springboard_image)
                                   ? springboard_imported_object(
                                         call,
                                         springboard_registration_status_import)
                                   : exported_object(
                                         call,
                                         "_kCTRegistrationStatusNotRegistered");
            call.set_return(token);
        });
    registry.register_function(
        std::string{core_telephony_image},
        "__CTServerConnectionSetCTMMode",
        [](UserlandHleCall& call) { return_server_success(call); });
    registry.register_function(
        std::string{core_telephony_image},
        "__CTServerConnectionNetworkTimeUpdatesAllowed",
        [](UserlandHleCall& call) { return_server_value(call, 0); });
    // Automatic carrier selection eventually dereferences the connection's
    // CommCenter CFMachPort. Baseband transport is intentionally parked, so
    // terminate the request at the user-space CoreTelephony boundary.
    registry.register_function(
        std::string{core_telephony_image},
        "__CTServerConnectionSelectNetwork",
        [](UserlandHleCall& call) { return_server_success(call); });

    // Let the firmware construct a genuine CFRuntime telephony-center object,
    // while suppressing only its attempt to establish the unavailable
    // CommCenter/baseband channel. Every client can retain/release it normally.
    registry.register_function(
        std::string{core_telephony_image}, "__EstablishServerConnection",
        [](UserlandHleCall& call) {
            if (is_offline_ui_client(call)) {
                call.set_return(0);
            } else {
                call.resume_original();
            }
        });
    registry.register_function(
        std::string{core_telephony_image}, "__KillServerConnection",
        [](UserlandHleCall& call) {
            if (is_offline_ui_client(call)) {
                call.set_return(0);
            } else {
                call.resume_original();
            }
        });
    for (const auto symbol : observer_operations) {
        registry.register_function(
            std::string{core_telephony_image}, std::string{symbol},
            [](UserlandHleCall& call) {
                if (is_offline_ui_client(call)) {
                    call.set_return(0);
                } else {
                    call.resume_original();
                }
            });
    }
}

}  // namespace ilegacysim
