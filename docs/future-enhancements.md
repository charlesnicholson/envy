# Future Enhancements

Potential enhancements not currently prioritized.

## Recipe Dependency Version Ranges

Support semver ranges for dependencies to reduce recipe churn when dependencies receive bug fixes. Trades reproducibility for convenience.

```lua
depends = { "vendor.library@^5.0.0" }  -- vs exact "vendor.library@5.0.0"
```

## Multi-File Recipes from Git Repositories

Fetch multi-file recipes directly from Git repos instead of requiring pre-packaged archives. Requires Git runtime dependency.

```lua
{ recipe = "vendor.gcc.v2@13.2.0", git = "https://github.com/vendor/recipes.git", ref = "v2.0" }
```

## Recipe Mirroring and Offline Support

Configure alternate download locations for air-gapped environments. Similar to npm registry mirrors or Go module proxies.

```lua
recipe_mirrors = { ["https://public.com/recipes/"] = "https://internal.corp/recipes/" }
```

## Recipe Deprecation Metadata

Mark recipes as deprecated with migration guidance. Envy warns users and suggests replacement.

```lua
deprecated = { message = "Use arm.gcc.v2 instead", replacement = "arm.gcc.v2" }
```

## Cross-Platform Recipe Variants

Higher-level abstraction for platform-specific variants within a single recipe identity. Current Lua approach handles this programmatically.
