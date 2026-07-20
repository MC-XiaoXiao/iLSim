#include "../suite.hpp"
#include "suite.hpp"

namespace ilegacysim::test::mach_suite {

void run_vm_tests() {
  run_vm_allocate_tests();
  run_vm_copy_tests();
  run_vm_memory_entry_tests();
  run_vm_purgable_tests();
  run_vm_read_tests();
}

} // namespace ilegacysim::test::mach_suite
