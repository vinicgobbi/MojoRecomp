#pragma once

// Reserves the guest's flat address space with physical-memory aliases: the
// range below the first view is ordinary zero-filled memory, and every view in
// `physicalViews` maps the same `physicalBytes` of shared backing, so a write
// through one view is visible through the others. Pages consume memory only
// when touched. One implementation per host, selected by CMake.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace mojorecomp::host {

// The views must be in ascending order, contiguous, and end exactly at
// `totalBytes`. Returns nullptr and describes the failure in `error`.
uint8_t* ReserveGuestAddressSpace(size_t totalBytes, size_t physicalBytes,
                                  std::span<const size_t> physicalViews,
                                  std::string& error);

} // namespace mojorecomp::host
