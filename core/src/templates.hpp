// Embedded template files: real .loo files saved by Looping, used to clone default objects.
#pragma once

#include <cstdint>
#include <span>

namespace loo {

// One entity (ids 1-4), one association (5-8) and one link between them (arcs 9-10, texts 11-16).
std::span<const std::uint8_t> linkTemplateBytes();
// One entity with a single default attribute `a` in its second text.
std::span<const std::uint8_t> attrTemplateBytes();

}  // namespace loo
