# Future Enhancements

This document tracks potential enhancements to Envy that are not currently prioritized but may be valuable in the future.

## Recipe Dependency Version Ranges

**Current behavior:** Recipe dependencies specify exact recipe identities and asset versions. If a dependency is updated (e.g., bug fix), the dependent recipe must be updated and republished.

**Enhancement:** Support semantic version ranges for recipe dependencies, allowing recipes to specify compatible version ranges rather than exact versions.

**Example:**
```lua
-- Current (exact versions)
depends = {
    "vendor.library@5.0.0",  -- Must update recipe to get 5.0.1
}

-- Enhanced (version ranges)
depends = {
    "vendor.library@^5.0.0",  -- Automatically uses latest 5.x.x
}
```

**Trade-offs:**
- **Pros:** Reduces need for recipe version bumps when dependencies receive bug fixes or non-breaking updates
- **Cons:** Sacrifices build reproducibility - same recipe version could resolve different dependencies over time
- **Cons:** Requires version comparison logic and resolution algorithm (similar to npm/cargo)
- **Cons:** Lockfiles or equivalent needed to restore reproducibility

**Notes:** The current exact-version approach prioritizes reproducible builds, which is critical for embedded/toolchain use cases. Version ranges may be valuable for other use cases but would require careful design to preserve reproducibility guarantees.

## Multi-File Recipes from Git Repositories

**Current behavior:** Multi-file recipes must be distributed as archives (`.tar.gz`, `.tar.xz`, `.zip`) that are downloaded via HTTPS.

**Enhancement:** Support fetching multi-file recipes directly from Git repositories.

**Example:**
```lua
-- Manifest
{
    recipe = "vendor.gcc.v2@13.2.0",
    git = "https://github.com/vendor/envy-recipes.git",
    ref = "v2.0",        -- tag, branch, or commit SHA
    path = "gcc/",       -- subdirectory containing recipe
    sha256 = "abc123...", -- hash of git tree at ref
}
```

**Benefits:**
- Easier recipe development and contribution (standard Git workflows)
- Recipe browsing on GitHub/GitLab without downloading
- Natural versioning via Git tags/branches
- Can fetch specific subdirectories from monorepos

**Trade-offs:**
- Requires Git as a runtime dependency (currently Envy only needs HTTPS)
- More complex caching and verification logic
- Git tree hashing differs from file hashing

**Implementation considerations:**
- Cache as `recipes/vendor.gcc.v2/` (same as archive extraction)
- Verify Git tree hash matches manifest's `sha256` field
- Support sparse checkouts for monorepo subdirectories
- Handle submodules if present (or explicitly disallow)

**Notes:** Archive-based distribution is simpler and doesn't require Git as a dependency. Git support could be added later if recipe authors prefer Git-based workflows over pre-packaging archives.

## Recipe Mirroring and Offline Support

**Enhancement:** Allow organizations to mirror recipe repositories and configure alternate download locations for air-gapped or offline environments.

**Example:**
```lua
-- Global or project config
recipe_mirrors = {
    ["https://public-vendor.com/recipes/"] = "https://internal-mirror.corp/recipes/",
}
```

**Notes:** Could leverage existing mirror/proxy patterns from other package managers (npm registry mirrors, Go module proxies, etc.).

## Recipe Deprecation and Migration Paths

**Enhancement:** Allow recipe authors to mark recipes as deprecated and provide migration guidance.

**Example:**
```lua
-- In old recipe
deprecated = {
    message = "This recipe is deprecated. Use arm.gcc.v2 instead.",
    replacement = "arm.gcc.v2",
}
```

Envy would warn users when loading deprecated recipes and suggest the replacement.

## Cross-Platform Recipe Variants

**Enhancement:** Support platform-specific recipe variants within a single recipe identity, rather than requiring separate recipes or complex conditionals.

**Notes:** The current Lua-based approach allows recipes to handle platform differences programmatically. This enhancement would provide a higher-level abstraction if platform-specific recipes become common.
