#include "bionic_math.h"

#include "guest_address_space.h"
#include "import_registry.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace isomr {
namespace {

template <typename T>
T VectorArgument(const Aarch64ImportRegistry::Registers& registers,
                 std::size_t index) {
    T value{};
    static_assert(sizeof(value) <= sizeof(registers.vector[index]));
    std::memcpy(&value, registers.vector[index].data(), sizeof(value));
    return value;
}

template <typename T>
void SetVectorResult(Aarch64ImportRegistry::Registers* registers, T value) {
    registers->vector[0] = {};
    std::memcpy(registers->vector[0].data(), &value, sizeof(value));
}

bool RegisterUnaryDouble(Aarch64ImportRegistry* registry, std::string name,
                         double (*operation)(double), std::string* error) {
    return registry->Register(
        std::move(name),
        [operation](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
            SetVectorResult(registers,
                            operation(VectorArgument<double>(*registers, 0)));
            return true;
        },
        error);
}

bool RegisterUnaryFloat(Aarch64ImportRegistry* registry, std::string name,
                        float (*operation)(float), std::string* error) {
    return registry->Register(
        std::move(name),
        [operation](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
            SetVectorResult(registers,
                            operation(VectorArgument<float>(*registers, 0)));
            return true;
        },
        error);
}

bool RegisterBinaryDouble(Aarch64ImportRegistry* registry, std::string name,
                          double (*operation)(double, double),
                          std::string* error) {
    return registry->Register(
        std::move(name),
        [operation](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
            SetVectorResult(
                registers,
                operation(VectorArgument<double>(*registers, 0),
                          VectorArgument<double>(*registers, 1)));
            return true;
        },
        error);
}

bool RegisterBinaryFloat(Aarch64ImportRegistry* registry, std::string name,
                         float (*operation)(float, float),
                         std::string* error) {
    return registry->Register(
        std::move(name),
        [operation](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
            SetVectorResult(
                registers,
                operation(VectorArgument<float>(*registers, 0),
                          VectorArgument<float>(*registers, 1)));
            return true;
        },
        error);
}

}  // namespace

bool RegisterBionicMathImports(Aarch64ImportRegistry* registry,
                               GuestAddressSpace* address_space,
                               std::string* error) {
    if (registry == nullptr || address_space == nullptr) return false;
    using UnaryDouble = double (*)(double);
    using UnaryFloat = float (*)(float);
    using BinaryDouble = double (*)(double, double);
    using BinaryFloat = float (*)(float, float);
    const struct {
        const char* name;
        UnaryDouble operation;
    } unary_double[]{{"acos", static_cast<UnaryDouble>(std::acos)},
                     {"asin", static_cast<UnaryDouble>(std::asin)},
                     {"atan", static_cast<UnaryDouble>(std::atan)},
                     {"cos", static_cast<UnaryDouble>(std::cos)},
                     {"cosh", static_cast<UnaryDouble>(std::cosh)},
                     {"exp", static_cast<UnaryDouble>(std::exp)},
                     {"exp2", static_cast<UnaryDouble>(std::exp2)},
                     {"log", static_cast<UnaryDouble>(std::log)},
                     {"log10", static_cast<UnaryDouble>(std::log10)},
                     {"log2", static_cast<UnaryDouble>(std::log2)},
                     {"logb", static_cast<UnaryDouble>(std::logb)},
                     {"sin", static_cast<UnaryDouble>(std::sin)},
                     {"sinh", static_cast<UnaryDouble>(std::sinh)},
                     {"tan", static_cast<UnaryDouble>(std::tan)},
                     {"tanh", static_cast<UnaryDouble>(std::tanh)}};
    const struct {
        const char* name;
        UnaryFloat operation;
    } unary_float[]{{"acosf", static_cast<UnaryFloat>(std::acos)},
                    {"asinf", static_cast<UnaryFloat>(std::asin)},
                    {"atanf", static_cast<UnaryFloat>(std::atan)},
                    {"cbrtf", static_cast<UnaryFloat>(std::cbrt)},
                    {"cosf", static_cast<UnaryFloat>(std::cos)},
                    {"exp2f", static_cast<UnaryFloat>(std::exp2)},
                    {"expf", static_cast<UnaryFloat>(std::exp)},
                    {"log10f", static_cast<UnaryFloat>(std::log10)},
                    {"log2f", static_cast<UnaryFloat>(std::log2)},
                    {"logf", static_cast<UnaryFloat>(std::log)},
                    {"nearbyintf", static_cast<UnaryFloat>(std::nearbyint)},
                    {"sinf", static_cast<UnaryFloat>(std::sin)},
                    {"sqrtf", static_cast<UnaryFloat>(std::sqrt)},
                    {"tanf", static_cast<UnaryFloat>(std::tan)}};
    const struct {
        const char* name;
        BinaryDouble operation;
    } binary_double[]{{"atan2", static_cast<BinaryDouble>(std::atan2)},
                      {"fmod", static_cast<BinaryDouble>(std::fmod)},
                      {"hypot", static_cast<BinaryDouble>(std::hypot)},
                      {"pow", static_cast<BinaryDouble>(std::pow)}};
    const struct {
        const char* name;
        BinaryFloat operation;
    } binary_float[]{{"atan2f", static_cast<BinaryFloat>(std::atan2)},
                     {"fmodf", static_cast<BinaryFloat>(std::fmod)},
                     {"hypotf", static_cast<BinaryFloat>(std::hypot)},
                     {"powf", static_cast<BinaryFloat>(std::pow)}};
    for (const auto& item : unary_double)
        if (!RegisterUnaryDouble(registry, item.name, item.operation, error))
            return false;
    for (const auto& item : unary_float)
        if (!RegisterUnaryFloat(registry, item.name, item.operation, error))
            return false;
    for (const auto& item : binary_double)
        if (!RegisterBinaryDouble(registry, item.name, item.operation, error))
            return false;
    for (const auto& item : binary_float)
        if (!RegisterBinaryFloat(registry, item.name, item.operation, error))
            return false;

    if (!registry->Register(
            "sincosf",
            [address_space](Aarch64ImportRegistry::Registers* registers,
                            std::string* dispatch_error) {
                const float input = VectorArgument<float>(*registers, 0);
                const float sine = std::sin(input);
                const float cosine = std::cos(input);
                if (!address_space->Write((*registers)[0], &sine,
                                          sizeof(sine)) ||
                    !address_space->Write((*registers)[1], &cosine,
                                          sizeof(cosine))) {
                    if (dispatch_error != nullptr)
                        *dispatch_error = "sincosf output pointer is invalid";
                    return false;
                }
                return true;
            },
            error) ||
        !registry->Register(
            "sincos",
            [address_space](Aarch64ImportRegistry::Registers* registers,
                            std::string* dispatch_error) {
                const double input = VectorArgument<double>(*registers, 0);
                const double sine = std::sin(input);
                const double cosine = std::cos(input);
                if (!address_space->Write((*registers)[0], &sine,
                                          sizeof(sine)) ||
                    !address_space->Write((*registers)[1], &cosine,
                                          sizeof(cosine))) {
                    if (dispatch_error != nullptr)
                        *dispatch_error = "sincos output pointer is invalid";
                    return false;
                }
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "frexp",
            [address_space](Aarch64ImportRegistry::Registers* registers,
                            std::string* dispatch_error) {
                int exponent = 0;
                const double result = std::frexp(
                    VectorArgument<double>(*registers, 0), &exponent);
                if (!address_space->Write((*registers)[0], &exponent,
                                          sizeof(exponent))) {
                    if (dispatch_error != nullptr)
                        *dispatch_error = "frexp exponent pointer is invalid";
                    return false;
                }
                SetVectorResult(registers, result);
                return true;
            },
            error) ||
        !registry->Register(
            "ldexp",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                SetVectorResult(registers,
                                std::ldexp(VectorArgument<double>(*registers, 0),
                                           static_cast<int>((*registers)[0])));
                return true;
            },
            error) ||
        !registry->Register(
            "ldexpf",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                SetVectorResult(registers,
                                std::ldexp(VectorArgument<float>(*registers, 0),
                                           static_cast<int>((*registers)[0])));
                return true;
            },
            error) ||
        !registry->Register(
            "scalbn",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                SetVectorResult(registers,
                                std::scalbn(VectorArgument<double>(*registers, 0),
                                            static_cast<int>((*registers)[0])));
                return true;
            },
            error) ||
        !registry->Register(
            "scalbnf",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                SetVectorResult(registers,
                                std::scalbn(VectorArgument<float>(*registers, 0),
                                            static_cast<int>((*registers)[0])));
                return true;
            },
            error) ||
        !registry->Register(
            "scalbln",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                SetVectorResult(
                    registers,
                    std::scalbln(VectorArgument<double>(*registers, 0),
                                 static_cast<long>((*registers)[0])));
                return true;
            },
            error) ||
        !registry->Register(
            "scalblnf",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                SetVectorResult(
                    registers,
                    std::scalbln(VectorArgument<float>(*registers, 0),
                                 static_cast<long>((*registers)[0])));
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "modf",
            [address_space](Aarch64ImportRegistry::Registers* registers,
                            std::string* dispatch_error) {
                double integral = 0;
                const double result = std::modf(
                    VectorArgument<double>(*registers, 0), &integral);
                if (!address_space->Write((*registers)[0], &integral,
                                          sizeof(integral))) {
                    if (dispatch_error != nullptr)
                        *dispatch_error = "modf output pointer is invalid";
                    return false;
                }
                SetVectorResult(registers, result);
                return true;
            },
            error) ||
        !registry->Register(
            "modff",
            [address_space](Aarch64ImportRegistry::Registers* registers,
                            std::string* dispatch_error) {
                float integral = 0;
                const float result = std::modf(
                    VectorArgument<float>(*registers, 0), &integral);
                if (!address_space->Write((*registers)[0], &integral,
                                          sizeof(integral))) {
                    if (dispatch_error != nullptr)
                        *dispatch_error = "modff output pointer is invalid";
                    return false;
                }
                SetVectorResult(registers, result);
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "nan",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                SetVectorResult(registers,
                                std::numeric_limits<double>::quiet_NaN());
                return true;
            },
            error))
        return false;
    return registry->Register(
        "nanf",
        [](Aarch64ImportRegistry::Registers* registers, std::string*) {
            SetVectorResult(registers,
                            std::numeric_limits<float>::quiet_NaN());
            return true;
        },
        error);
}

}  // namespace isomr
