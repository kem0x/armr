#pragma once

#include <string>

namespace isomr {

class Aarch64ImportRegistry;
class GuestHeap;

bool RegisterBionicMemoryImports(Aarch64ImportRegistry* imports,
                                 GuestHeap* heap, std::string* error);

}  // namespace isomr
