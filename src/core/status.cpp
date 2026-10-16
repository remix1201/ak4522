#include "ak4522/status.hpp"

namespace ak4522 {

std::string_view toString(Status status) noexcept
{
    switch (status) {
    case Status::ok: return "ok";
    case Status::notPrepared: return "not prepared";
    case Status::invalidDacRate: return "DAC rate outside 16000 to 48000 Hz, or 50400 Hz when extended";
    case Status::invalidMasterClock: return "master clock outside the range for the CMODE ratio";
    case Status::unsupportedHostRate: return "unsupported host sample rate";
    case Status::unsupportedRatePair: return "unsupported host and DAC rate combination";
    case Status::invalidBlockSize: return "invalid maximum block size";
    case Status::invalidConfiguration: return "invalid configuration";
    case Status::channelSizeMismatch: return "left and right channel lengths differ";
    case Status::insufficientOutputCapacity: return "output buffer too small";
    case Status::eventOutOfRange: return "event offset outside the block";
    case Status::eventsNotSorted: return "event offsets are not in order";
    case Status::formatMismatch: return "input sample type does not match the configured format";
    case Status::modeMismatch: return "push called on a pull-mode stream";
    }
    return "unknown status";
}

} // namespace ak4522
