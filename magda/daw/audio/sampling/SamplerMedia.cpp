#include "SamplerMedia.hpp"

namespace magda {

SamplerMedia& SamplerMedia::getInstance() {
    static SamplerMedia media;
    return media;
}

}  // namespace magda
