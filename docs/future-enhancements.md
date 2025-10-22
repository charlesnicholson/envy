# Future Enhancements

Potential enhancements not currently prioritized.

## Manifest Transform Hooks

Beyond declarative overrides, allow manifests to programmatically transform recipe specifications. Provides maximum flexibility for complex scenarios.

```lua
-- project/envy.lua
function transform_recipe(spec)
  -- Redirect all recipes to internal mirror
  if spec.url and spec.url:match("^https://example.com/") then
    spec.url = spec.url:gsub("^https://example.com/", "https://internal-mirror.company/")
  end

  -- Force specific version for security
  if spec.recipe == "openssl.lib@v3" then
    spec.options = spec.options or {}
    spec.options.version = "3.0.12"  -- Known secure version
  end

  return spec
end

packages = { "openssl.lib@v3", "curl.tool@v2" }
```

**Considerations:** Hook executes during manifest validation. Applied to all recipe specs (packages + transitive dependencies) before override resolution. Must be pure function (no side effects). Ordering: transform → override → validation.

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
