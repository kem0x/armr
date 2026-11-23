#include "bionic_properties.h"

#include "guest_address_space.h"
#include "import_registry.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace isomr {
namespace {

constexpr std::size_t kMaximumPropertyName = 128;
constexpr std::size_t kMaximumPropertyValue = 92;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

bool ReadPropertyName(const GuestAddressSpace& address_space,
                      std::uint64_t address, std::string* name,
                      std::string* error) {
    if (address == 0 || name == nullptr) {
        SetError(error, "system property name pointer is invalid");
        return false;
    }
    name->clear();
    for (std::size_t index = 0; index < kMaximumPropertyName; ++index) {
        char value = 0;
        if (!address_space.Read(address + index, &value, sizeof(value))) {
            SetError(error, "system property name is unreadable");
            return false;
        }
        if (value == '\0') return !name->empty();
        name->push_back(value);
    }
    SetError(error, "system property name exceeds its bound");
    return false;
}

std::string_view PropertyValue(std::string_view name) {
    if (name == "ro.build.version.sdk") return "35";
    if (name == "ro.build.version.release") return "15";
    if (name == "ro.build.version.codename") return "REL";
    if (name == "ro.product.cpu.abi" ||
        name == "ro.product.cpu.abilist" ||
        name == "ro.product.cpu.abilist64")
        return "arm64-v8a";
    if (name == "ro.debuggable") return "0";
    if (name == "ro.secure") return "1";
    if (name == "ro.product.brand" || name == "ro.product.device" ||
        name == "ro.product.manufacturer" || name == "ro.product.model")
        return "ARMR";
    return {};
}

}  // namespace

bool RegisterBionicPropertyImports(Aarch64ImportRegistry* registry,
                                   GuestAddressSpace* address_space,
                                   std::string* error) {
    if (registry == nullptr || address_space == nullptr) return false;
    return registry->Register(
        "__system_property_get",
        [address_space](Aarch64ImportRegistry::Registers* registers,
                        std::string* dispatch_error) {
            std::string name;
            if (!ReadPropertyName(*address_space, (*registers)[0], &name,
                                  dispatch_error))
                return false;
            const std::string_view value = PropertyValue(name);
            if (value.size() + 1 > kMaximumPropertyValue) {
                SetError(dispatch_error,
                         "system property value exceeds its ABI bound");
                return false;
            }
            const char terminator = '\0';
            if ((*registers)[1] == 0 ||
                !address_space->Write((*registers)[1], value.data(),
                                      value.size()) ||
                !address_space->Write((*registers)[1] + value.size(),
                                      &terminator, sizeof(terminator))) {
                SetError(dispatch_error,
                         "system property output buffer is invalid");
                return false;
            }
            (*registers)[0] = value.size();
            return true;
        },
        error);
}

}  // namespace isomr
