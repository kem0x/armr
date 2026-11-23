#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace isomr {

class Aarch64ImportRegistry;

struct BionicFinalizer {
    std::uint64_t function = 0;
    std::uint64_t argument = 0;
    std::uint64_t dso_handle = 0;
};

class BionicAtExit {
public:
    explicit BionicAtExit(std::size_t capacity = 4096)
        : capacity_(capacity) {}

    bool RegisterImports(Aarch64ImportRegistry* registry,
                         std::string* error);
    std::vector<BionicFinalizer> DrainFinalizerPass();
    bool CompleteFinalizer();
    std::size_t DiscardForProcessExit();
    std::size_t size() const;

private:
    std::size_t capacity_ = 0;
    mutable std::mutex mutex_;
    std::vector<BionicFinalizer> finalizers_;
    std::deque<BionicFinalizer> pending_finalizers_;
};

}  // namespace isomr
