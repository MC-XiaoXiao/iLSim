#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <ostream>
#include <string_view>

namespace ilegacysim {

class Output {
public:
    explicit Output(std::ostream& stream);
    explicit Output(const std::filesystem::path& path);

    void write(std::string_view text);
    void line(std::string_view text);

private:
    std::unique_ptr<std::ofstream> file_;
    std::ostream* stream_{};
    std::mutex mutex_;
};

}  // namespace ilegacysim

