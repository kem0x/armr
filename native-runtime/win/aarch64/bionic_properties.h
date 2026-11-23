#pragma once

#include <string>

namespace isomr {

class Aarch64ImportRegistry;
class GuestAddressSpace;

bool RegisterBionicPropertyImports(Aarch64ImportRegistry* registry,
                                   GuestAddressSpace* address_space,
                                   std::string* error);

}  // namespace isomr
