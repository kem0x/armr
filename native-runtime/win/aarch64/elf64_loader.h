#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace isomr {

constexpr std::uint32_t kElfSegmentExecute = 1;
constexpr std::uint32_t kElfSegmentWrite = 2;
constexpr std::uint32_t kElfSegmentRead = 4;

struct Elf64Segment {
    std::uint64_t virtual_address = 0;
    std::uint64_t memory_size = 0;
    std::uint32_t flags = 0;
};

struct Elf64ProgramHeader {
    std::uint32_t type = 0;
    std::uint32_t flags = 0;
    std::uint64_t offset = 0;
    std::uint64_t virtual_address = 0;
    std::uint64_t physical_address = 0;
    std::uint64_t file_size = 0;
    std::uint64_t memory_size = 0;
    std::uint64_t alignment = 0;
};

static_assert(sizeof(Elf64ProgramHeader) == 56);

struct Elf64RelocationStats {
    std::size_t relative = 0;
    std::size_t absolute = 0;
    std::size_t global_data = 0;
    std::size_t jump_slots = 0;
    std::size_t imports_bound = 0;
};

struct Elf64LoadResult;
using Elf64ImportResolver =
    std::function<std::optional<std::uint64_t>(std::string_view)>;
using Elf64DependencyPreparer =
    std::function<bool(const std::vector<std::string>&, std::string*)>;

class Elf64Image {
public:
    std::uint64_t entry_point() const { return entry_point_; }
    std::uint64_t guest_base() const { return guest_base_; }
    std::uint64_t lowest_virtual_address() const { return low_vaddr_; }
    std::uint64_t load_bias() const { return guest_base_ - low_vaddr_; }
    std::size_t mapped_size() const { return memory_.size(); }
    const std::vector<Elf64Segment>& segments() const { return segments_; }
    const std::vector<Elf64ProgramHeader>& program_headers() const {
        return program_headers_;
    }
    const Elf64RelocationStats& relocations() const { return relocations_; }
    const std::vector<std::string>& needed() const { return needed_; }
    const std::vector<std::uint64_t>& initializers() const {
        return initializers_;
    }

    std::optional<std::uint64_t> GuestAddress(
        std::uint64_t virtual_address) const;
    std::optional<std::uint64_t> FindExport(std::string_view name) const;
    bool Read(std::uint64_t guest_address, void* destination,
              std::size_t size) const;
    bool ReadCode(std::uint64_t guest_address, void* destination,
                  std::size_t size) const;
    bool PatchCode(
        std::uint64_t guest_address,
        const std::array<std::uint32_t, 2>& expected,
        const std::array<std::uint32_t, 2>& replacement,
        std::string* error);
    bool IsWritableRange(std::uint64_t guest_address,
                         std::size_t size) const;
    bool Write(std::uint64_t guest_address, const void* source,
               std::size_t size);

private:
    friend class Elf64LoaderImpl;
    friend struct Elf64LoadResult;
    friend Elf64LoadResult LoadElf64File(
        const std::wstring&, std::uint64_t, const Elf64ImportResolver&,
        const Elf64DependencyPreparer&);

    bool RangeOffset(std::uint64_t guest_address, std::size_t size,
                     std::uint32_t required_flags,
                     std::size_t* offset) const;

    std::uint64_t guest_base_ = 0;
    std::uint64_t low_vaddr_ = 0;
    std::uint64_t entry_point_ = 0;
    std::vector<std::uint8_t> memory_;
    std::vector<Elf64Segment> segments_;
    std::vector<Elf64ProgramHeader> program_headers_;
    std::vector<std::string> needed_;
    std::vector<std::uint64_t> initializers_;
    std::unordered_map<std::string, std::uint64_t> exports_;
    Elf64RelocationStats relocations_;
};

struct Elf64LoadResult {
    std::optional<Elf64Image> image;
    std::string error;
};

Elf64LoadResult LoadElf64File(const std::wstring& path,
                              std::uint64_t guest_base,
                              const Elf64ImportResolver& resolver,
                              const Elf64DependencyPreparer& prepare = {});

}  // namespace isomr
