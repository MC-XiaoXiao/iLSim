#include "ilegacysim/cpu.hpp"

#include <array>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>

namespace ilegacysim {

class Cpu::Callbacks final : public Dynarmic::A32::UserCallbacks {
public:
    explicit Callbacks(AddressSpace& memory) : memory_{memory} {}

    void attach(Cpu* owner, Dynarmic::A32::Jit* jit) {
        owner_ = owner;
        jit_ = jit;
    }

    bool PreCodeReadHook(
        bool, Dynarmic::A32::VAddr, Dynarmic::A32::IREmitter&) override {
        // This fork's translator continues normal decoding when the hook returns
        // true. Returning false is reserved for a hook that already emitted an IR
        // terminal. (The comment in UserCallbacks currently says the opposite.)
        return true;
    }

    std::optional<std::uint32_t> MemoryReadCode(std::uint32_t address) override {
        const auto value = memory_.read32(address, MemoryPermission::Execute);
        if (!value) {
            memory_fault(address, 4, MemoryPermission::Execute);
        }
        return value;
    }

    std::uint8_t MemoryRead8(std::uint32_t address) override {
        return read<std::uint8_t>(address, &AddressSpace::read8);
    }
    std::uint16_t MemoryRead16(std::uint32_t address) override {
        return read<std::uint16_t>(address, &AddressSpace::read16);
    }
    std::uint32_t MemoryRead32(std::uint32_t address) override {
        return read<std::uint32_t>(address, &AddressSpace::read32);
    }
    std::uint64_t MemoryRead64(std::uint32_t address) override {
        return read<std::uint64_t>(address, &AddressSpace::read64);
    }

    void MemoryWrite8(std::uint32_t address, std::uint8_t value) override {
        write(address, value, &AddressSpace::write8);
    }
    void MemoryWrite16(std::uint32_t address, std::uint16_t value) override {
        write(address, value, &AddressSpace::write16);
    }
    void MemoryWrite32(std::uint32_t address, std::uint32_t value) override {
        write(address, value, &AddressSpace::write32);
    }
    void MemoryWrite64(std::uint32_t address, std::uint64_t value) override {
        write(address, value, &AddressSpace::write64);
    }

    std::uint8_t MemorySwap8(
        std::uint32_t address, std::uint8_t value) override {
        return swap(address, value, &AddressSpace::exchange8);
    }
    std::uint32_t MemorySwap32(
        std::uint32_t address, std::uint32_t value) override {
        return swap(address, value, &AddressSpace::exchange32);
    }

    bool MemoryWriteExclusive8(
        std::uint32_t address, std::uint8_t value, std::uint8_t expected) override {
        const auto written = memory_.compare_exchange8(address, expected, value);
        if (written) notify_memory_write(address, sizeof(value), value);
        return written;
    }
    bool MemoryWriteExclusive16(
        std::uint32_t address, std::uint16_t value, std::uint16_t expected) override {
        const auto written = memory_.compare_exchange16(address, expected, value);
        if (written) notify_memory_write(address, sizeof(value), value);
        return written;
    }
    bool MemoryWriteExclusive32(
        std::uint32_t address, std::uint32_t value, std::uint32_t expected) override {
        const auto written = memory_.compare_exchange32(address, expected, value);
        if (written) notify_memory_write(address, sizeof(value), value);
        return written;
    }
    bool MemoryWriteExclusive64(
        std::uint32_t address, std::uint64_t value, std::uint64_t expected) override {
        const auto written = memory_.compare_exchange64(address, expected, value);
        if (written) notify_memory_write(address, sizeof(value), value);
        return written;
    }

    bool IsReadOnlyMemory(std::uint32_t) override { return false; }

    void InterpreterFallback(std::uint32_t pc, std::size_t count) override {
        std::ostringstream message;
        message << "Dynarmic interpreter fallback at 0x" << std::hex << pc
                << " for " << std::dec << count << " instruction(s)";
        exception_ = message.str();
        jit_->HaltExecution(Dynarmic::HaltReason::UserDefined3);
    }

    void CallSVC(std::uint32_t immediate) override {
        svc_ = immediate;
        if (svc_dispatch_mode_ == SvcDispatchMode::Deferred) {
            jit_->HaltExecution(Dynarmic::HaltReason::UserDefined2);
            return;
        }
        if (svc_handler_) {
            svc_handler_(*owner_, immediate);
        } else {
            jit_->HaltExecution(Dynarmic::HaltReason::UserDefined2);
        }
    }

    void ExceptionRaised(std::uint32_t pc, Dynarmic::A32::Exception exception) override {
        if (exception == Dynarmic::A32::Exception::Breakpoint &&
            debug_breakpoints_enabled_) {
            breakpoint_ = pc;
            owner_->registers()[15] = pc;
            jit_->HaltExecution(Dynarmic::HaltReason::UserDefined7);
            return;
        }
        std::ostringstream message;
        message << "ARM exception " << static_cast<unsigned>(exception)
                << " at 0x" << std::hex << pc;
        exception_ = message.str();
        jit_->HaltExecution(Dynarmic::HaltReason::UserDefined3);
    }

    void AddTicks(std::uint64_t ticks) override {
        consumed_ += ticks;
        ticks_remaining_ = ticks >= ticks_remaining_ ? 0 : ticks_remaining_ - ticks;
    }
    std::uint64_t GetTicksRemaining() override { return ticks_remaining_; }

    void begin(std::uint64_t ticks) {
        ticks_remaining_ = ticks;
        consumed_ = 0;
        svc_.reset();
        fault_.reset();
        breakpoint_.reset();
        exception_.clear();
    }

    CpuRunResult result(Dynarmic::HaltReason reason) const {
        return CpuRunResult{
            reason, consumed_, svc_, fault_, breakpoint_, exception_};
    }

    void set_svc_handler(SvcHandler handler) { svc_handler_ = std::move(handler); }
    void set_svc_dispatch_mode(SvcDispatchMode mode) {
        svc_dispatch_mode_ = mode;
    }
    void set_memory_write_watchpoint(
        std::uint32_t address, Cpu::MemoryWriteHandler handler) {
        memory_write_watch_address_ = address;
        memory_write_handler_ = std::move(handler);
    }
    void set_debug_breakpoints_enabled(bool enabled) {
        debug_breakpoints_enabled_ = enabled;
    }

private:
    template<typename T, typename Member>
    T read(std::uint32_t address, Member member) {
        const auto value = (memory_.*member)(address, MemoryPermission::Read);
        if (!value) {
            memory_fault(address, sizeof(T), MemoryPermission::Read);
            return 0;
        }
        return *value;
    }

    template<typename T, typename Member>
    void write(std::uint32_t address, T value, Member member) {
        if (!(memory_.*member)(address, value)) {
            memory_fault(address, sizeof(T), MemoryPermission::Write);
        } else {
            notify_memory_write(address, sizeof(T), value);
        }
    }

    template<typename T, typename Member>
    T swap(std::uint32_t address, T value, Member member) {
        const auto previous = (memory_.*member)(address, value);
        if (!previous) {
            memory_fault(address, sizeof(T),
                         MemoryPermission::Read | MemoryPermission::Write);
            return 0;
        }
        notify_memory_write(address, sizeof(T), value);
        return *previous;
    }

    void notify_memory_write(
        std::uint32_t address, std::size_t size, std::uint64_t value) {
        if (!memory_write_watch_address_ || !memory_write_handler_) return;
        const auto write_begin = static_cast<std::uint64_t>(address);
        const auto write_end = write_begin + size;
        const auto watched = static_cast<std::uint64_t>(*memory_write_watch_address_);
        if (watched >= write_begin && watched < write_end) {
            memory_write_handler_(*owner_, address, size, value);
        }
    }

    void memory_fault(std::uint32_t address, std::size_t size, MemoryPermission access) {
        fault_ = MemoryFault{address, size, access, "unmapped address or protection failure"};
        if (jit_ != nullptr) {
            jit_->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
        }
    }

    AddressSpace& memory_;
    Cpu* owner_{};
    Dynarmic::A32::Jit* jit_{};
    std::uint64_t ticks_remaining_{};
    std::uint64_t consumed_{};
    std::optional<std::uint32_t> svc_;
    std::optional<MemoryFault> fault_;
    std::optional<std::uint32_t> breakpoint_;
    std::string exception_;
    SvcHandler svc_handler_;
    SvcDispatchMode svc_dispatch_mode_{SvcDispatchMode::Immediate};
    std::optional<std::uint32_t> memory_write_watch_address_;
    Cpu::MemoryWriteHandler memory_write_handler_;
    bool debug_breakpoints_enabled_{};
};

Cpu::Cpu(
    std::size_t processor_id, AddressSpace& memory, Dynarmic::ExclusiveMonitor& monitor)
    : processor_id_{processor_id}, monitor_{&monitor},
      callbacks_{std::make_unique<Callbacks>(memory)} {}

void Cpu::ensure_jit() {
    if (jit_) return;
    Dynarmic::A32::UserConfig config{callbacks_.get()};
    config.processor_id = processor_id_;
    config.global_monitor = monitor_;
    config.arch_version = Dynarmic::A32::ArchVersion::v6K;
    config.always_little_endian = true;
    config.enable_cycle_counting = true;
    config.check_halt_on_memory_access = true;
    jit_ = std::make_unique<Dynarmic::A32::Jit>(config);
    callbacks_->attach(this, jit_.get());
}

Cpu::~Cpu() = default;

CpuRunResult Cpu::run(std::uint64_t ticks) {
    ensure_jit();
    callbacks_->begin(ticks);
    return callbacks_->result(jit_->Run());
}

CpuRunResult Cpu::step() {
    ensure_jit();
    callbacks_->begin(1);
    return callbacks_->result(jit_->Step());
}

void Cpu::reset() {
    ensure_jit();
    jit_->Reset();
}
void Cpu::clear_cache() {
    if (jit_) jit_->ClearCache();
}
void Cpu::invalidate_cache_range(std::uint32_t address, std::size_t length) {
    if (jit_ && length != 0) {
        jit_->InvalidateCacheRange(address, length);
    }
}
void Cpu::clear_halt() {
    requested_halt_reason_ = {};
    if (!jit_) return;
    jit_->ClearHalt(Dynarmic::HaltReason::MemoryAbort |
                    Dynarmic::HaltReason::UserDefined1 |
                    Dynarmic::HaltReason::UserDefined2 |
                    Dynarmic::HaltReason::UserDefined3 |
                    Dynarmic::HaltReason::UserDefined4 |
                    Dynarmic::HaltReason::UserDefined5 |
                    Dynarmic::HaltReason::UserDefined6 |
                    Dynarmic::HaltReason::UserDefined7 |
                    Dynarmic::HaltReason::UserDefined8);
}
void Cpu::halt(Dynarmic::HaltReason reason) {
    requested_halt_reason_ = requested_halt_reason_ | reason;
    if (jit_) jit_->HaltExecution(reason);
}

Dynarmic::HaltReason Cpu::consume_requested_halt_reason() {
    const auto reason = requested_halt_reason_;
    requested_halt_reason_ = {};
    return reason;
}

std::array<std::uint32_t, 16>& Cpu::registers() {
    ensure_jit();
    return jit_->Regs();
}
const std::array<std::uint32_t, 16>& Cpu::registers() const {
    const_cast<Cpu*>(this)->ensure_jit();
    return jit_->Regs();
}
std::uint32_t Cpu::cpsr() const {
    const_cast<Cpu*>(this)->ensure_jit();
    return jit_->Cpsr();
}
void Cpu::set_cpsr(std::uint32_t value) {
    ensure_jit();
    jit_->SetCpsr(value);
}
void Cpu::set_svc_handler(SvcHandler handler) { callbacks_->set_svc_handler(std::move(handler)); }
void Cpu::set_svc_dispatch_mode(SvcDispatchMode mode) {
    callbacks_->set_svc_dispatch_mode(mode);
}
void Cpu::set_memory_write_watchpoint(
    std::uint32_t address, MemoryWriteHandler handler) {
    callbacks_->set_memory_write_watchpoint(address, std::move(handler));
}
void Cpu::set_debug_breakpoints_enabled(bool enabled) {
    callbacks_->set_debug_breakpoints_enabled(enabled);
}

CpuCluster::CpuCluster(std::size_t processor_count, AddressSpace& memory)
    : monitor_{processor_count} {
    if (processor_count == 0) {
        throw std::invalid_argument{"processor_count must be at least one"};
    }
    cpus_.reserve(processor_count);
    for (std::size_t id = 0; id < processor_count; ++id) {
        cpus_.push_back(std::make_unique<Cpu>(id, memory, monitor_));
    }
}

std::vector<CpuRunResult> CpuCluster::run_parallel(std::uint64_t ticks_per_cpu) {
    std::vector<CpuRunResult> results(cpus_.size());
    std::vector<std::thread> workers;
    workers.reserve(cpus_.size());
    for (std::size_t index = 0; index < cpus_.size(); ++index) {
        workers.emplace_back([&, index] { results[index] = cpus_[index]->run(ticks_per_cpu); });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    return results;
}

}  // namespace ilegacysim
