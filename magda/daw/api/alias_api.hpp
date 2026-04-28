#pragma once

namespace magda {

class AliasRegistry;
class ResolverRegistry;

/**
 * Abstract view onto the alias / resolver registries.
 *
 * PR1 just exposes the two registry references — TargetResolver consumes
 * them by reference and isn't worth wrapping further at this stage. If
 * a future MagdaApiPlugin needs different alias semantics we'll factor
 * resolveSigil into this interface, but for now this preserves the
 * existing call shape.
 */
class AliasApi {
  public:
    virtual ~AliasApi() = default;

    virtual AliasRegistry& aliasRegistry() = 0;
    virtual ResolverRegistry& resolverRegistry() = 0;
};

}  // namespace magda
