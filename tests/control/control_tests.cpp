#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "ilegacysim/display.hpp"
#include "ilegacysim/graphics_services_input.hpp"
#include "ilegacysim/kernel_shared_state.hpp"
#include "ilegacysim/live_control.hpp"
#include "ilegacysim/live_touch_scheduler.hpp"
#include "test_support.hpp"

namespace {

using namespace ilegacysim;
using ilegacysim::test::require;

std::uint32_t read_word(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t result = 0;
  for (std::size_t index = 0; index < sizeof(result); ++index) {
    result |= std::to_integer<std::uint32_t>(bytes[offset + index])
              << (index * 8U);
  }
  return result;
}

std::vector<LiveControlCommand> parse_commands(const std::string &input) {
  int descriptors[2]{};
  require(::pipe(descriptors) == 0, "failed to create control test pipe");
  const auto written = ::write(descriptors[1], input.data(), input.size());
  ::close(descriptors[1]);
  if (written != static_cast<ssize_t>(input.size())) {
    ::close(descriptors[0]);
    throw std::runtime_error{"failed to write control test input"};
  }
  LiveControl control{descriptors[0]};
  auto commands = control.poll();
  ::close(descriptors[0]);
  return commands;
}

void tap_gesture_test() {
  const auto commands = parse_commands("tap 40 75\n");
  require(commands.size() == 1 &&
              commands[0].kind == LiveControlCommandKind::Gesture &&
              commands[0].message == "tap" && commands[0].gesture.size() == 2,
          "tap was not parsed as one scheduled gesture");
  const auto &down = commands[0].gesture[0];
  const auto &up = commands[0].gesture[1];
  require(down.delay == std::chrono::milliseconds{0} &&
              down.input.phase == TouchPhase::Down && down.input.x == 40.0F &&
              down.input.y == 75.0F &&
              up.delay == std::chrono::milliseconds{80} &&
              up.input.phase == TouchPhase::Up && up.input.x == 40.0F &&
              up.input.y == 75.0F,
          "tap did not preserve coordinates and the default hold interval");

  const auto held = parse_commands("tap 295 228 125\n");
  require(held.size() == 1 && held[0].gesture.size() == 2 &&
              held[0].gesture[1].delay == std::chrono::milliseconds{125},
          "tap did not accept an explicit hold interval");
}

void unlock_gesture_test() {
  const auto commands = parse_commands("unlock\n");
  require(commands.size() == 1 &&
              commands[0].kind == LiveControlCommandKind::Gesture &&
              commands[0].message == "unlock" && commands[0].wake_display &&
              commands[0].gesture.size() == 9,
          "unlock preset did not produce a complete slider gesture");
  const auto &gesture = commands[0].gesture;
  require(gesture.front().input.phase == TouchPhase::Down &&
              gesture.front().input.x == 50.0F &&
              gesture.front().input.y == 430.0F &&
              gesture.back().input.phase == TouchPhase::Up &&
              gesture.back().input.x == 260.0F &&
              gesture.back().input.y == 430.0F &&
              gesture.front().delay == std::chrono::milliseconds{1000} &&
              gesture.back().delay == std::chrono::milliseconds{2600},
          "unlock preset missed the native lock slider endpoints");
  for (std::size_t index = 1; index < gesture.size(); ++index) {
    require(gesture[index].delay > gesture[index - 1].delay,
            "unlock gesture deadlines are not strictly increasing");
  }
}

void wake_command_test() {
  const auto commands = parse_commands("wake\nlock\nwake now\nlock now\n");
  require(commands.size() == 4 &&
              commands[0].kind == LiveControlCommandKind::Wake &&
              commands[1].kind == LiveControlCommandKind::Lock &&
              commands[2].kind == LiveControlCommandKind::Error &&
              commands[3].kind == LiveControlCommandKind::Error,
          "physical-button commands did not enforce their argument contract");
}

void invalid_control_test() {
  const auto commands =
      parse_commands("tap 320 20\nunlock now\ndrag 10 10 20 20 2 3\n");
  require(commands.size() == 3,
          "invalid control input did not return one result per line");
  for (const auto &command : commands) {
    require(command.kind == LiveControlCommandKind::Error,
            "invalid control input was accepted");
  }
}

void scheduler_spacing_test() {
  const auto commands = parse_commands("tap 40 75 40\n");
  LiveTouchScheduler scheduler;
  scheduler.schedule(commands[0].gesture);
  const auto immediate = scheduler.poll();
  require(immediate.size() == 1 && immediate[0].phase == TouchPhase::Down,
          "tap scheduler emitted Down and Up in the same poll");
  std::this_thread::sleep_for(std::chrono::milliseconds{60});
  const auto released = scheduler.poll();
  require(released.size() == 1 && released[0].phase == TouchPhase::Up &&
              scheduler.empty(),
          "tap scheduler did not emit the delayed Up event");
}

void graphics_services_wire_test() {
  KernelSharedState state;
  constexpr std::uint32_t event_port_object = 0x123400U;
  state.bootstrap_service_objects.emplace(
      std::string{graphics_services_input::system_event_service},
      event_port_object);
  state.clock.advance_to(0x1122334455667788ULL);

  constexpr float x = 123.25F;
  constexpr float y = 321.5F;
  const std::array inputs{
      TouchInput{TouchPhase::Down, x, y},
      TouchInput{TouchPhase::Move, x + 1.0F, y},
      TouchInput{TouchPhase::Up, x + 1.0F, y},
  };
  for (const auto &input : inputs) {
    require(graphics_services_input::enqueue_touch(state, input) ==
                graphics_services_input::EnqueueResult::Queued,
            "touch event was not queued to the resolved firmware port");
  }

  const auto &queue = state.mach_queues.at(event_port_object);
  require(queue.size() == inputs.size(),
          "firmware touch queue has an unexpected event count");
  constexpr std::array<std::uint32_t, 3> hand_types{1, 2, 5};
  for (std::size_t index = 0; index < queue.size(); ++index) {
    const auto &message = queue[index];
    require(message.bytes.size() == 108 && read_word(message.bytes, 4) == 108 &&
                read_word(message.bytes, 8) == event_port_object &&
                read_word(message.bytes, 20) == 123 &&
                graphics_services_input::event_type(message.bytes) == 3001U,
            "touch message does not match the iPhone OS 1.0 GSEvent envelope");
    require(read_word(message.bytes, 72) == hand_types[index] &&
                std::to_integer<std::uint8_t>(message.bytes[89]) == 1 &&
                std::to_integer<std::uint8_t>(message.bytes[92]) == 1 &&
                std::to_integer<std::uint8_t>(message.bytes[93]) == 2,
            "touch message does not preserve the firmware hand path ABI");
    const auto active = index != 2;
    require(std::to_integer<std::uint8_t>(message.bytes[94]) ==
                    (active ? 3 : 0) &&
                std::bit_cast<float>(read_word(message.bytes, 96)) ==
                    (active ? 1.0F : 0.0F) &&
                std::bit_cast<float>(read_word(message.bytes, 100)) ==
                    inputs[index].x &&
                std::bit_cast<float>(read_word(message.bytes, 104)) ==
                    inputs[index].y,
            "touch message proximity, pressure, or coordinates are invalid");
    require(read_word(message.bytes, 48) == 0x55667788U &&
                read_word(message.bytes, 52) == 0x11223344U,
            "touch message timestamp is not the guest monotonic clock");
  }
}

void graphics_services_system_button_wire_test() {
  KernelSharedState state;
  constexpr std::uint32_t event_port_object = 0x123500U;
  state.bootstrap_service_objects.emplace(
      std::string{graphics_services_input::system_event_service},
      event_port_object);
  state.clock.advance_to(0x0102030405060708ULL);

  constexpr std::array inputs{
      SystemButtonInput{SystemButton::Home, SystemButtonPhase::Down},
      SystemButtonInput{SystemButton::Home, SystemButtonPhase::Up},
      SystemButtonInput{SystemButton::VolumeUp, SystemButtonPhase::Down},
      SystemButtonInput{SystemButton::VolumeUp, SystemButtonPhase::Up},
      SystemButtonInput{SystemButton::VolumeDown, SystemButtonPhase::Down},
      SystemButtonInput{SystemButton::VolumeDown, SystemButtonPhase::Up},
      SystemButtonInput{SystemButton::Lock, SystemButtonPhase::Down},
      SystemButtonInput{SystemButton::Lock, SystemButtonPhase::Up},
  };
  for (const auto &input : inputs) {
    require(graphics_services_input::enqueue_system_button(state, input) ==
                graphics_services_input::EnqueueResult::Queued,
            "system button was not queued to the firmware event port");
  }
  const auto &queue = state.mach_queues.at(event_port_object);
  require(queue.size() == inputs.size(),
          "system buttons have an unexpected event count");
  constexpr std::array<std::uint32_t, 8> event_types{1000, 1001, 1006, 1007,
                                                     1008, 1009, 1010, 1011};
  for (std::size_t index = 0; index < queue.size(); ++index) {
    const auto &message = queue[index];
    require(message.bytes.size() == 72 && read_word(message.bytes, 4) == 72 &&
                read_word(message.bytes, 8) == event_port_object &&
                read_word(message.bytes, 20) == 123 &&
                graphics_services_input::event_type(message.bytes) ==
                    event_types[index] &&
                read_word(message.bytes, 48) == 0x05060708U &&
                read_word(message.bytes, 52) == 0x01020304U,
            "Home button message does not match the simple GSEvent ABI");
  }
}

void graphics_services_application_routing_test() {
  KernelSharedState state;
  constexpr std::uint32_t system_object = 0x123600U;
  constexpr std::uint32_t application_object = 0x123700U;
  constexpr std::uint32_t application_pid = 30U;
  constexpr std::uint32_t springboard_pid = 15U;
  constexpr std::uint32_t lookup_reply_object = 0x123800U;
  state.bootstrap_service_objects.emplace(
      std::string{graphics_services_input::system_event_service},
      system_object);
  require(state.mach_port_objects.create(application_object, application_pid),
          "could not create the application event port fixture");
  state.processes[application_pid].executable_path =
      "/Applications/TestApp.app/TestApp";
  state.processes[springboard_pid].executable_path =
      "/System/Library/CoreServices/SpringBoard.app/SpringBoard";
  graphics_services_input::record_bootstrap_lookup_locked(
      state, lookup_reply_object, "org.example.TestApp");
  const std::array transfers{KernelSharedState::MachMessage::PortTransfer{
      0U, 0U, std::nullopt, application_object, xnu792::ipc::Right::Send, 19U}};
  const auto resolution =
      graphics_services_input::record_bootstrap_reply_locked(
          state, lookup_reply_object, transfers);
  require(resolution.object == application_object &&
              resolution.application_event_port &&
              resolution.service_name == "org.example.TestApp",
          "bootstrap lookup did not recognize the application event port");

  require(graphics_services_input::enqueue_touch(
              state, TouchInput{TouchPhase::Down, 10.0F, 20.0F}) ==
                  graphics_services_input::EnqueueResult::Queued &&
              state.mach_queues.at(system_object).size() == 1U &&
              !state.mach_queues.contains(application_object),
          "a background application service stole SpringBoard touch input");
  {
    std::lock_guard lock{state.mach_mutex};
    graphics_services_input::record_application_lifecycle_event_locked(
        state, springboard_pid, application_object, 50U);
  }
  graphics_services_input::activate_resolved_application(state,
                                                         application_pid);
  require(graphics_services_input::enqueue_touch(
              state, TouchInput{TouchPhase::Up, 10.0F, 20.0F}) ==
                  graphics_services_input::EnqueueResult::Queued &&
              state.mach_queues.at(system_object).size() == 2U &&
              !state.mach_queues.contains(application_object),
          "lifecycle activation without a visible App scene stole touch input");
  graphics_services_input::record_pending_application_scene_translation(state,
                                                                        20);
  graphics_services_input::record_pending_application_scene_translation(state,
                                                                        0);

  require(graphics_services_input::enqueue_touch(
              state, TouchInput{TouchPhase::Down, 25.0F, 50.0F}) ==
                  graphics_services_input::EnqueueResult::Queued &&
              state.mach_queues.at(application_object).size() == 1U &&
              state.mach_queues.at(system_object).size() == 2U &&
              std::bit_cast<float>(read_word(
                  state.mach_queues.at(application_object).back().bytes, 36)) ==
                  30.0F,
          "foreground touch did not route directly to the application port");
  require(graphics_services_input::enqueue_system_button(
              state,
              SystemButtonInput{SystemButton::Home, SystemButtonPhase::Down}) ==
                  graphics_services_input::EnqueueResult::Queued &&
              state.mach_queues.at(system_object).size() == 3U,
          "physical input incorrectly followed the application touch route");

  graphics_services_input::suspend_active_application(state);
  require(graphics_services_input::enqueue_touch(
              state, TouchInput{TouchPhase::Up, 25.0F, 50.0F}) ==
                  graphics_services_input::EnqueueResult::Queued &&
              state.mach_queues.at(system_object).size() == 4U,
          "suspending the foreground application did not restore system input");

  {
    std::lock_guard lock{state.mach_mutex};
    graphics_services_input::record_application_lifecycle_event_locked(
        state, springboard_pid, application_object, 50U);
  }
  require(graphics_services_input::enqueue_touch(
              state, TouchInput{TouchPhase::Down, 30.0F, 60.0F}) ==
                  graphics_services_input::EnqueueResult::Queued &&
              state.mach_queues.at(application_object).size() == 2U &&
              !state.latest_application_scene_translation &&
              state.active_application_scene &&
              state.active_application_scene->process_id == application_pid &&
              state.active_application_scene->event_object ==
                  application_object &&
              state.active_application_scene->screen_to_client_y == 20,
          "foreground lifecycle event did not resume application touch");

  {
    std::lock_guard lock{state.mach_mutex};
    std::vector<std::uint32_t> exit_snapshot_pixels(
        static_cast<std::size_t>(iphone_2g_display_width) *
            iphone_2g_display_height,
        0U);
    for (std::uint32_t y = 0; y < iphone_2g_display_height; ++y) {
      std::fill_n(exit_snapshot_pixels.begin() +
                      static_cast<std::ptrdiff_t>(
                          y * iphone_2g_display_width),
                  iphone_2g_display_width, 0xff000000U | y);
    }
    graphics_services_input::record_application_lifecycle_event_locked(
        state, springboard_pid, application_object, 2002U,
        exit_snapshot_pixels);
  }
  require(graphics_services_input::enqueue_touch(
              state, TouchInput{TouchPhase::Up, 30.0F, 60.0F}) ==
                  graphics_services_input::EnqueueResult::Queued &&
              state.mach_queues.at(system_object).size() == 5U &&
              state.pending_application_exit_snapshot &&
              state.pending_application_exit_snapshot->process_id ==
                  application_pid &&
              state.pending_application_exit_snapshot->pixels.size() ==
                  static_cast<std::size_t>(iphone_2g_display_width) *
                      iphone_2g_display_height &&
              state.pending_application_exit_snapshot->pixels.front() ==
                  0xff000014U &&
              state.pending_application_exit_snapshot
                      ->pixels[240U * iphone_2g_display_width] ==
                  0xff0000faU &&
              state.pending_application_exit_snapshot->pixels.back() ==
                  0xff0001dfU,
          "resign-active lifecycle event did not preserve App-local pixels");
}

void display_power_gating_test() {
  DisplayState display;
  DisplayFrame presented;
  display.set_presenter(
      [&presented](const DisplayFrame &frame) { presented = frame; });
  display.clear(0xff123456U);
  display.present();
  require(display.powered_on() && !presented.pixels.empty() &&
              presented.pixels.front() == 0xff123456U,
          "powered display did not expose its scanout pixels");

  display.set_powered_on(false);
  const auto sleeping = display.snapshot();
  require(!display.powered_on() && !sleeping.pixels.empty() &&
              sleeping.pixels.front() == 0xff000000U &&
              presented.pixels.front() == 0xff000000U,
          "sleeping display leaked its stale scanout frame");

  display.set_powered_on(true);
  const auto awake = display.snapshot();
  require(display.powered_on() && awake.pixels.front() == 0xff123456U &&
              presented.pixels.front() == 0xff123456U,
          "display wake did not restore the retained scanout frame");
}

void run_tests() {
  tap_gesture_test();
  unlock_gesture_test();
  wake_command_test();
  invalid_control_test();
  scheduler_spacing_test();
  graphics_services_wire_test();
  graphics_services_system_button_wire_test();
  graphics_services_application_routing_test();
  display_power_gating_test();
}

} // namespace

int main() { return ilegacysim::test::run_suite("control", run_tests); }
