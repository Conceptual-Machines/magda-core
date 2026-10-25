// JUCE's VST3 wrapper always answers initialize() with kResultTrue, so the one place a
// JUCE plugin can refuse an instance the way a host sees it is the factory (#3155).

#include <juce_audio_processors/juce_audio_processors.h>

#undef JUCE_VST3HEADERS_INCLUDE_HEADERS_ONLY
#define JUCE_VST3HEADERS_INCLUDE_HEADERS_ONLY 1
#include <juce_audio_processors_headless/format_types/juce_VST3Headers.h>
#undef JUCE_VST3HEADERS_INCLUDE_HEADERS_ONLY

#include <atomic>

#include "DebugPluginFaults.hpp"

// JUCE's export, renamed by the target's compile definitions.
#undef GetPluginFactory

namespace magda::debug_plugin {

namespace {

using namespace Steinberg;

/// JUCE's factory with every call forwarded, except a component past max-instances.
class RefusingFactory final : public IPluginFactory3 {
  public:
    explicit RefusingFactory(IPluginFactory* inner) : inner_(inner) {
        inner_->queryInterface(IPluginFactory3::iid, reinterpret_cast<void**>(&inner3_));
    }

    ~RefusingFactory() {
        if (inner3_ != nullptr)
            inner3_->release();
        inner_->release();
    }

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, FUnknown::iid) ||
            FUnknownPrivate::iidEqual(iid, IPluginFactory::iid) ||
            (inner3_ != nullptr && (FUnknownPrivate::iidEqual(iid, IPluginFactory2::iid) ||
                                    FUnknownPrivate::iidEqual(iid, IPluginFactory3::iid)))) {
            addRef();
            *obj = this;
            return kResultOk;
        }

        *obj = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef() override {
        return ++refs_;
    }

    uint32 PLUGIN_API release() override {
        const auto remaining = --refs_;
        if (remaining == 0)
            delete this;
        return remaining;
    }

    tresult PLUGIN_API getFactoryInfo(PFactoryInfo* info) override {
        return inner_->getFactoryInfo(info);
    }

    int32 PLUGIN_API countClasses() override {
        return inner_->countClasses();
    }

    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo* info) override {
        return inner_->getClassInfo(index, info);
    }

    tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, Vst::IComponent::iid)) {
            const auto faults = Faults::fromEnvironment();
            if (faults.maxInstances > 0 && liveInstances().load() >= faults.maxInstances) {
                *obj = nullptr;
                return kResultFalse;
            }
        }

        return inner_->createInstance(cid, iid, obj);
    }

    tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2* info) override {
        return inner3_->getClassInfo2(index, info);
    }

    tresult PLUGIN_API getClassInfoUnicode(int32 index, PClassInfoW* info) override {
        return inner3_->getClassInfoUnicode(index, info);
    }

    tresult PLUGIN_API setHostContext(FUnknown* context) override {
        return inner3_->setHostContext(context);
    }

  private:
    IPluginFactory* inner_;
    IPluginFactory3* inner3_ = nullptr;
    std::atomic<uint32> refs_{1};
};

}  // namespace

}  // namespace magda::debug_plugin

extern "C" SMTG_EXPORT_SYMBOL Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory() {
    return new magda::debug_plugin::RefusingFactory(JuceGetPluginFactory());
}
