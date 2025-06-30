#pragma once

#include <string>
#include <vector>

/**
 * @brief Interface for controlling mixing parameters
 * 
 * The MixerInterface provides methods for controlling volume, pan, 
 * and effects routing for tracks.
 */
class MixerInterface {
public:
    virtual ~MixerInterface() = default;
    
    /**
     * @brief Set track volume
     * @param track_id The track ID
     * @param volume Volume level (0.0 = silence, 1.0 = unity gain)
     */
    virtual void setTrackVolume(const std::string& track_id, double volume) = 0;
    
    /**
     * @brief Get track volume
     */
    virtual double getTrackVolume(const std::string& track_id) const = 0;
    
    /**
     * @brief Set track pan
     * @param track_id The track ID
     * @param pan Pan position (-1.0 = full left, 0.0 = center, 1.0 = full right)
     */
    virtual void setTrackPan(const std::string& track_id, double pan) = 0;
    
    /**
     * @brief Get track pan
     */
    virtual double getTrackPan(const std::string& track_id) const = 0;
    
    /**
     * @brief Set master volume
     * @param volume Master volume level
     */
    virtual void setMasterVolume(double volume) = 0;
    
    /**
     * @brief Get master volume
     */
    virtual double getMasterVolume() const = 0;
    
    /**
     * @brief Add an effect to a track
     * @param track_id The track ID
     * @param effect_name Name of the effect to add
     * @return Effect instance ID
     */
    virtual std::string addEffect(const std::string& track_id, const std::string& effect_name) = 0;
    
    /**
     * @brief Remove an effect from a track
     * @param effect_id The effect instance ID to remove
     */
    virtual void removeEffect(const std::string& effect_id) = 0;
    
    /**
     * @brief Set effect parameter
     * @param effect_id The effect instance ID
     * @param parameter_name The parameter name
     * @param value The parameter value (normalized 0.0-1.0)
     */
    virtual void setEffectParameter(const std::string& effect_id, 
                                   const std::string& parameter_name, 
                                   double value) = 0;
    
    /**
     * @brief Get effect parameter
     */
    virtual double getEffectParameter(const std::string& effect_id, 
                                     const std::string& parameter_name) const = 0;
    
    /**
     * @brief Enable/disable an effect
     */
    virtual void setEffectEnabled(const std::string& effect_id, bool enabled) = 0;
    
    /**
     * @brief Check if effect is enabled
     */
    virtual bool isEffectEnabled(const std::string& effect_id) const = 0;
    
    /**
     * @brief Get list of available effects
     */
    virtual std::vector<std::string> getAvailableEffects() const = 0;
    
    /**
     * @brief Get list of effects on a track
     */
    virtual std::vector<std::string> getTrackEffects(const std::string& track_id) const = 0;
}; 