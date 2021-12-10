#ifndef NBOUL_SAMPLE_SAMPLE_H
#define NBOUL_SAMPLE_SAMPLE_H

#include <stdint.h>
#include <limits>

namespace nboul {
namespace sample {

using Sample = uint16_t;
using DoubleSample = uint32_t;

inline Sample clipped(DoubleSample dSample)
{
	if (dSample < std::numeric_limits<Sample>::min())
		return std::numeric_limits<Sample>::min();
	else if (dSample > std::numeric_limits<Sample>::max())
		return std::numeric_limits<Sample>::max();
	else
		return static_cast<Sample>(dSample);
}

}  // namespace sample
}  // namespace nboul

#endif  // NBOUL_SAMPLE_SAMPLE_H
