# Future Enhancements

Potential enhancements not currently prioritized.

## Recipe Version Ranges

Support semver ranges for recipe dependencies to reduce churn when recipe bugs are fixed. Recipe versions must be semver-compliant to enable ranges.

```lua
depends = { "vendor.library@^2.0.0" }  -- Any 2.x recipe version
```

## Multi-File Recipes from Git Repositories

Fetch multi-file recipes directly from Git repos instead of requiring pre-packaged archives. Requires Git runtime dependency.

```lua
{ recipe = "vendor.gcc@v2", git = "https://github.com/vendor/recipes.git", ref = "v2.0" }
```

## Recipe Mirroring and Offline Support

Configure alternate download locations for air-gapped environments. Similar to npm registry mirrors or Go module proxies.

```lua
recipe_mirrors = { ["https://public.com/recipes/"] = "https://internal.corp/recipes/" }
```

## Recipe Deprecation Metadata

Mark recipes as deprecated with migration guidance. Envy warns users and suggests replacement.

```lua
deprecated = { message = "Use arm.gcc@v2 instead", replacement = "arm.gcc@v2" }
```

## Cross-Platform Recipe Variants

Higher-level abstraction for platform-specific variants within a single recipe identity. Current Lua approach handles this programmatically.
