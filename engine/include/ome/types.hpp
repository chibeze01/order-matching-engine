#ifndef OME_TYPES_HPP
#define OME_TYPES_HPP

// Placeholder public header for the ome_core library.
//
// TODO(SPA-2): define the core value types here. Integer-tick Price, Quantity,
// OrderId, Side, and the Order struct all live in this header once SPA-2 lands.
// See Linear issue SPA-2 for the acceptance criteria.

namespace ome {

// Returns the library version string. This is a trivial placeholder so that
// ome_core exposes a linkable symbol and CI has something green to assert on
// from the very first commit. It goes away once real types arrive.
const char *library_version();

} // namespace ome

#endif // OME_TYPES_HPP
