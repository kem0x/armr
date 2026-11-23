#include "bionic_atexit.h"

#include "import_registry.h"

namespace isomr {

bool BionicAtExit::RegisterImports(Aarch64ImportRegistry* registry,
                                   std::string* error) {
    if (registry == nullptr || capacity_ == 0) return false;
    const auto register_finalizer =
        [this](Aarch64ImportRegistry::Registers* registers, std::string*) {
            const std::lock_guard lock(mutex_);
            if ((*registers)[0] == 0 ||
                finalizers_.size() + pending_finalizers_.size() >=
                    capacity_) {
                (*registers)[0] = UINT64_MAX;
                return true;
            }
            finalizers_.push_back(
                {(*registers)[0], (*registers)[1], (*registers)[2]});
            (*registers)[0] = 0;
            return true;
        };
    // __cxa_thread_atexit_impl registers a thread_local destructor. The
    // guest runtime has no per-thread exit teardown, so TLS destructors are
    // deferred into the shared finalizer pass (process/unload scope), which
    // preserves their execution order without a TLS callback mechanism.
    return registry->Register("__cxa_atexit", register_finalizer, error) &&
           registry->Register("__cxa_thread_atexit_impl", register_finalizer,
                              error);
}

std::vector<BionicFinalizer> BionicAtExit::DrainFinalizerPass() {
    const std::lock_guard lock(mutex_);
    if (pending_finalizers_.empty()) {
        pending_finalizers_.insert(pending_finalizers_.end(),
                                   finalizers_.rbegin(),
                                   finalizers_.rend());
        finalizers_.clear();
    }
    return {pending_finalizers_.begin(), pending_finalizers_.end()};
}

bool BionicAtExit::CompleteFinalizer() {
    const std::lock_guard lock(mutex_);
    if (pending_finalizers_.empty()) return false;
    pending_finalizers_.pop_front();
    return true;
}

std::size_t BionicAtExit::DiscardForProcessExit() {
    const std::lock_guard lock(mutex_);
    const std::size_t discarded =
        finalizers_.size() + pending_finalizers_.size();
    finalizers_.clear();
    pending_finalizers_.clear();
    return discarded;
}

std::size_t BionicAtExit::size() const {
    const std::lock_guard lock(mutex_);
    return finalizers_.size() + pending_finalizers_.size();
}

}  // namespace isomr
