#pragma once

#include "import_registry.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace isomr {

class GuestAddressSpace;
class GuestHeap;

bool FormatBionicVariadicCall(
    GuestHeap* heap, GuestAddressSpace* memory,
    const Aarch64ImportRegistry::Registers& registers,
    std::size_t named_general_arguments, std::uint64_t format_address,
    std::string* output, std::string* error);

bool FormatBionicVaList(GuestAddressSpace* memory,
                        std::uint64_t format_address,
                        std::uint64_t list_address,
                        std::string* output, std::string* error);

// Runs the bionic scanf engine over a caller-supplied input buffer. The
// variadic output-pointer arguments are read from `registers` starting at
// general[2] (matching sscanf/fscanf where the format is the second named
// argument). When `consumed` is non-null it receives the number of input
// bytes consumed, so a stream-backed caller can advance the file position.
bool ScanBionicBuffer(GuestAddressSpace* memory, const std::string& input,
                      const std::string& format,
                      const Aarch64ImportRegistry::Registers& registers,
                      std::size_t* consumed, std::uint64_t* raw_result,
                      std::string* error);

bool RegisterBionicFormatImports(Aarch64ImportRegistry* registry,
                                  GuestHeap* heap,
                                  GuestAddressSpace* memory,
                                  std::string* error);

}  // namespace isomr
