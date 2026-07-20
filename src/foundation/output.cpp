#include "ilegacysim/output.hpp"

#include <stdexcept>

namespace ilegacysim {

Output::Output(std::ostream& stream) : stream_{&stream} {}

Output::Output(const std::filesystem::path& path)
    : file_{std::make_unique<std::ofstream>(path, std::ios::binary | std::ios::trunc)},
      stream_{file_.get()} {
    if (!*file_) {
        throw std::runtime_error{"cannot open output file: " + path.string()};
    }
}

void Output::write(std::string_view text) {
    std::lock_guard lock{mutex_};
    stream_->write(text.data(), static_cast<std::streamsize>(text.size()));
    stream_->flush();
}

void Output::line(std::string_view text) {
    std::lock_guard lock{mutex_};
    stream_->write(text.data(), static_cast<std::streamsize>(text.size()));
    stream_->put('\n');
    stream_->flush();
}

}  // namespace ilegacysim

