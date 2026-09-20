#include "GrooveLibrary.hpp"

namespace magda {

GrooveLibrary& GrooveLibrary::getInstance() {
    static GrooveLibrary library;
    return library;
}

}  // namespace magda
