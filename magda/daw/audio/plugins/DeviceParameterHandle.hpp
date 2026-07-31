#pragma once

namespace magda::daw::audio {

/**
 * Non-owning, engine-neutral access to a hosted device parameter.
 *
 * Device SDK interfaces return this value instead of exposing the parameter
 * class owned by the active audio engine. The host adapter supplies the
 * callbacks when it creates the handle.
 */
class DeviceParameterHandle {
  public:
    using ReadCallback = float (*)(const void*);
    using WriteCallback = void (*)(void*, float);

    constexpr DeviceParameterHandle() = default;

    constexpr DeviceParameterHandle(void* parameter, ReadCallback readBaseValue,
                                    ReadCallback readCurrentValue, WriteCallback writeValue)
        : parameter_(parameter),
          readBaseValue_(readBaseValue),
          readCurrentValue_(readCurrentValue),
          writeValue_(writeValue) {}

    constexpr explicit operator bool() const {
        return parameter_ != nullptr;
    }

    float currentBaseValue() const {
        return parameter_ != nullptr && readBaseValue_ != nullptr ? readBaseValue_(parameter_)
                                                                  : 0.0f;
    }

    float currentValue() const {
        return parameter_ != nullptr && readCurrentValue_ != nullptr ? readCurrentValue_(parameter_)
                                                                     : 0.0f;
    }

    void setValueFromHost(float value) const {
        if (parameter_ != nullptr && writeValue_ != nullptr)
            writeValue_(parameter_, value);
    }

    /**
     * Opaque adapter payload. Only an engine integration layer should inspect
     * this value; device-pack code must use the operations above.
     */
    constexpr void* nativeHandle() const {
        return parameter_;
    }

  private:
    void* parameter_ = nullptr;
    ReadCallback readBaseValue_ = nullptr;
    ReadCallback readCurrentValue_ = nullptr;
    WriteCallback writeValue_ = nullptr;
};

}  // namespace magda::daw::audio
