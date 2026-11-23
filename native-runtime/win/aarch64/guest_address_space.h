#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace isomr {

class Elf64ModuleSet;
class GuestHeap;

class GuestAddressSpace {
public:
    explicit GuestAddressSpace(GuestHeap* heap,
                               std::size_t maximum_transfer =
                                   64U * 1024U * 1024U,
                               std::size_t maximum_string = 1024U * 1024U);
    ~GuestAddressSpace();

    bool AttachModules(Elf64ModuleSet* modules, std::string* error);
    bool Read(std::uint64_t address, void* destination, std::size_t size) const;
    bool Write(std::uint64_t address, const void* source, std::size_t size);
    bool IsWritableRange(std::uint64_t address, std::size_t size) const;
    std::string DescribeRange(std::uint64_t address,
                              std::size_t size) const;

    bool Copy(std::uint64_t destination, std::uint64_t source,
              std::uint64_t size, std::string* error);
    bool Fill(std::uint64_t destination, std::uint8_t value,
              std::uint64_t size, std::string* error);
    bool Compare(std::uint64_t left, std::uint64_t right, std::uint64_t size,
                 int* result, std::string* error) const;
    bool FindByte(std::uint64_t address, std::uint8_t value,
                  std::uint64_t size, std::uint64_t* result,
                  std::string* error) const;
    bool StringLength(std::uint64_t address, std::uint64_t* result,
                      std::string* error) const;
    bool BoundedStringLength(std::uint64_t address, std::uint64_t maximum,
                             std::uint64_t* result,
                             std::string* error) const;
    bool StringCompare(std::uint64_t left, std::uint64_t right, int* result,
                       std::string* error) const;
    bool BoundedStringCompare(std::uint64_t left, std::uint64_t right,
                              std::uint64_t maximum, int* result,
                              std::string* error) const;

    GuestHeap* heap() const { return heap_; }

private:
    bool CheckedSize(std::uint64_t size, std::size_t* checked,
                     std::string* error) const;
    bool ScanStringLength(std::uint64_t address, std::uint64_t bound,
                          std::uint64_t* result) const;
    bool self_valid() const;

    std::uint64_t self_magic_ = 0;
    GuestHeap* heap_ = nullptr;
    Elf64ModuleSet* modules_ = nullptr;
    std::size_t maximum_transfer_ = 0;
    std::size_t maximum_string_ = 0;
};

}  // namespace isomr
