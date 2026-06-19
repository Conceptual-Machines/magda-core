# dawproject

This directory contains the minimal dawproject specification files needed by
Magda for import/export validation.

Upstream: https://github.com/bitwig/dawproject

Imported from upstream commit:

```
ee4dcdde75940f30e14e55401a26955a58b8322b
```

Included files:

- `Project.xsd`
- `MetaData.xsd`
- `LICENSE`

The upstream Java implementation, Gradle build, generated sources, and tests are
intentionally not vendored. Magda's C++ dawproject support should use these XSD
files as the local validation contract.
