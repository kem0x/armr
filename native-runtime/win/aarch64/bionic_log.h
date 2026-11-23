#pragma once

#include "import_registry.h"

#include <cstddef>
#include <mutex>
#include <string>

namespace isomr {

class GuestAddressSpace;
class GuestHeap;

class BionicLog {
public:
    BionicLog(GuestHeap* heap, GuestAddressSpace* memory);

    bool RegisterImports(Aarch64ImportRegistry* imports,
                         std::string* error);
    std::size_t call_count() const;

private:
    bool Print(Aarch64ImportRegistry::Registers* registers,
               std::string* error);
    bool VPrint(Aarch64ImportRegistry::Registers* registers,
                std::string* error);
    bool Write(Aarch64ImportRegistry::Registers* registers,
               std::string* error);
    bool SetAbortMessage(Aarch64ImportRegistry::Registers* registers,
                         std::string* error);
    bool Syslog(Aarch64ImportRegistry::Registers* registers,
                std::string* error);
    bool VSyslog(Aarch64ImportRegistry::Registers* registers,
                 std::string* error);
    bool SyslogChk(Aarch64ImportRegistry::Registers* registers,
                   std::string* error);
    void MaybeDumpStack(Aarch64ImportRegistry::Registers* registers,
                        char level, const std::string& tag,
                        const std::string& message);

    GuestHeap* heap_ = nullptr;
    GuestAddressSpace* memory_ = nullptr;
    const Aarch64ImportRegistry* imports_ = nullptr;
    mutable std::mutex mutex_;
    std::size_t call_count_ = 0;
    std::string syslog_ident_;
    std::uint64_t syslog_mask_ = 0xff;
    bool registration_attempted_ = false;
};

}  // namespace isomr
