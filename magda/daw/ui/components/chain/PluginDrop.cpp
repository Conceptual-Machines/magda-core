#include "PluginDrop.hpp"

namespace magda::daw::ui {

magda::DeviceInfo deviceInfoFromPluginDrag(const juce::DynamicObject& obj) {
    magda::DeviceInfo device;
    device.name = obj.getProperty("name").toString().toStdString();
    device.manufacturer = obj.getProperty("manufacturer").toString().toStdString();
    auto uniqueId = obj.getProperty("uniqueId").toString();
    device.pluginId = uniqueId.isNotEmpty() ? uniqueId
                                            : obj.getProperty("name").toString() + "_" +
                                                  obj.getProperty("format").toString();
    const auto rawCategory = obj.hasProperty("rawCategory")
                                 ? obj.getProperty("rawCategory").toString()
                                 : obj.getProperty("category").toString();
    const auto rawSubcategory = obj.hasProperty("rawSubcategory")
                                    ? obj.getProperty("rawSubcategory").toString()
                                    : obj.getProperty("subcategory").toString();
    device.isInstrument = rawCategory.isNotEmpty()
                              ? rawCategory == "Instrument"
                              : static_cast<bool>(obj.getProperty("isInstrument"));
    if (rawSubcategory == "MIDI")
        device.deviceType = magda::DeviceType::MIDI;
    else if (device.isInstrument)
        device.deviceType = magda::DeviceType::Instrument;
    device.browserCategoryOverride = obj.getProperty("categoryOverride").toString();
    device.uniqueId = obj.getProperty("uniqueId").toString();
    device.fileOrIdentifier = obj.getProperty("fileOrIdentifier").toString();

    const juce::String format = obj.getProperty("format").toString();
    if (format == "VST3")
        device.format = magda::PluginFormat::VST3;
    else if (format == "AU")
        device.format = magda::PluginFormat::AU;
    else if (format == "LV2")
        device.format = magda::PluginFormat::LV2;
    else if (format == "Internal")
        device.format = magda::PluginFormat::Internal;

    return device;
}

}  // namespace magda::daw::ui
