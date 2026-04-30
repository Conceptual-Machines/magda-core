#pragma once

namespace magda {

/**
 * Transport control surface — play / stop / record / loop / position.
 *
 * Position read/write is in beats, consistent with the rest of MAGDA's
 * coordinate system. Implementations are responsible for the seconds↔beats
 * conversion via the project's tempo sequence; callers don't see it.
 *
 * The live impl reaches into `edit->getTransport()`; in headless / no-edit
 * states queries return safe defaults (false, 0.0) and writes are no-ops
 * rather than crashes.
 */
class TransportApi {
  public:
    virtual ~TransportApi() = default;

    /** Start playback from the current edit position. */
    virtual void play() = 0;

    /** Stop playback (and recording, if active). */
    virtual void stop() = 0;

    /** Toggle recording on/off. */
    virtual void setRecording(bool recording) = 0;

    virtual bool isPlaying() const = 0;
    virtual bool isRecording() const = 0;

    virtual bool isLoopEnabled() const = 0;
    virtual void setLoopEnabled(bool enabled) = 0;

    /** Edit position in beats. Returns 0 if no edit is loaded. */
    virtual double getPositionBeats() const = 0;
    virtual void setPositionBeats(double beats) = 0;
};

}  // namespace magda
