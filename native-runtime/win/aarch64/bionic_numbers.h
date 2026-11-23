#pragma once

#include <string>

namespace isomr {

class Aarch64ImportRegistry;
class BionicLocales;
class BionicThreadState;
class GuestAddressSpace;

bool RegisterBionicNumberImports(Aarch64ImportRegistry* registry,
                                 GuestAddressSpace* address_space,
                                 BionicThreadState* threads,
                                 BionicLocales* locales,
                                 std::string* error);

}  // namespace isomr
