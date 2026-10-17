#pragma once

namespace magda::engine {

/** @brief Something the record thread moves to disk, a round at a time. */
class RecordDrain {
  public:
    virtual ~RecordDrain() = default;

    /// One round of work on the record thread; whether it did any.
    virtual bool drain() = 0;
};

}  // namespace magda::engine
