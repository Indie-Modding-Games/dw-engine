#pragma once

#include <dwe/formats/video/mdec.hpp>

// Offline clean-up of decoded movie frames (an engine improvement; docs/formats/video.md,
// «Restauración»): the blocking and ringing that coarse quantisation leaves, taken out with the
// post filters of MPEG-4 (ISO/IEC 14496-2, annex F.3), whose strength follows the frame's
// quantisation scale. The MDEC's scale has the meaning of MPEG's quantiser (an AC step of
// scale * weight / 8), so the annex's thresholds apply as written, in sixteenths of a level.
// Integer arithmetic only: the same frame always gives the same samples.
namespace dwe::formats::video {

// Smooths the 8x8 block edges of every plane. Each line across an edge (five samples on each
// side) is either flat, where a 9-tap low-pass evens out the step between the blocks when it is
// smaller than twice the scale, or detailed, where only the edge's own step is reduced, by no more
// than the quantisation can explain and never more than half the step. Vertical edges first, then
// horizontal ones.
void deblock(PreciseFrame& frame);

// Mild deringing inside each 8x8 block: the samples on either side of the block's mid level are
// smoothed with a 3x3 kernel only where all nine neighbours lie on the same side (away from the
// block's edges of detail), and no sample moves by more than half the scale.
void dering(PreciseFrame& frame);

} // namespace dwe::formats::video
