# Third-Party Dependencies

## picojson

**Version:** 1.3.0
**License:** BSD-2-Clause
**Purpose:** Single-header JSON parser/serializer—parse, edit, serialize `.luarc.json` and structured JSON output
**Integration:** Single header downloaded to `out/cache/picojson-src/`
**Rationale:** Smallest viable JSON library (34 KB); replaces bespoke JSON output in `cmd_product.cpp` and enables safe round-trip editing of user `.luarc.json` files

## Sol2

**Version:** c1f95a773c6f8f4fde8ca3efe872e7286afe4444
**License:** MIT
**Purpose:** Header-only C++/Lua bindings—type-safe stack manipulation, STL interop
**Integration:** Amalgamation generated via `single.py` during CMake configure
**Rationale:** Eliminates error-prone manual stack manipulation; enables idiomatic C++ patterns for Lua integration
