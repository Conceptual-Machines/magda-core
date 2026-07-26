#include "ParameterInfo.hpp"

#include <atomic>

namespace magda {

namespace {

std::atomic<ParameterDisplayTextProviderFactory> providerFactory{nullptr};

}  // namespace

bool registerParameterDisplayTextProviderFactory(ParameterDisplayTextProviderFactory factory) {
    if (factory == nullptr)
        return false;

    auto expected = static_cast<ParameterDisplayTextProviderFactory>(nullptr);
    return providerFactory.compare_exchange_strong(expected, factory) || expected == factory;
}

std::shared_ptr<ParameterInfo::DisplayTextProvider> makeParameterDisplayTextProvider(
    const ChainNodePath& devicePath, int deviceId, int paramIndex) {
    if (const auto factory = providerFactory.load())
        return factory(devicePath, deviceId, paramIndex);
    return {};
}

}  // namespace magda
