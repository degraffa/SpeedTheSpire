// FIXTURE, not a compiled translation unit. It exercises the documented escape
// hatch of the boundary check (tools/check_omniscient_boundary.sh):  [omniscient-boundary-ok]
// a comment that names the other (full-state) observation surface in order to
// contrast with it, carrying the hatch token on the SAME line, must not fail
// the check. The two lines below are exactly that shape.
//
// Contrast: the other encoder is omniscient_encode_observation ...  [omniscient-boundary-ok]
// ... and the record it fills is OmniscientObsBuffer.  [omniscient-boundary-ok]
//
// The hatch is LINE-scoped on purpose: it excuses the mention, not the file.
// Nothing here reaches the other surface -- this fixture reads PublicView.

#include "sts/engine/public_view.hpp"
