#include "suite.hpp"
#include "test_support.hpp"

namespace ilegacysim::test::network_suite {

void run_tests() {
  run_abi_route_tests();
  run_host_socket_tests();
  run_wifi_tests();
  run_virtual_udp_tests();
  run_event_tests();
  run_kernel_control_tests();
  run_unix_socket_tests();
}

} // namespace ilegacysim::test::network_suite

int main() {
  return ilegacysim::test::run_suite(
      "network_suite", ilegacysim::test::network_suite::run_tests);
}
