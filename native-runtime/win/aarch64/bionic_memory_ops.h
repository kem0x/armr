#pragma once

#include <string>

namespace isomr {

class Aarch64ImportRegistry;
class GuestAddressSpace;
class GuestHeap;

bool RegisterBionicMemoryOperationImports(Aarch64ImportRegistry* imports,
                                          GuestAddressSpace* memory,
                                          std::string* error);

bool RegisterBionicMemoryOperationImports(
    Aarch64ImportRegistry* imports, GuestAddressSpace* memory,
    GuestHeap* heap, std::string* error);

bool RegisterStringNCopies(Aarch64ImportRegistry* imports,
                           GuestAddressSpace* memory, std::string* error);

}  // namespace isomr
