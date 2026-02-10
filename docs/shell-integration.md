# Shell Integration

Envy shell hooks auto-manage `PATH` and `ENVY_PROJECT_ROOT` as you navigate envy projects. No envy binary required at shell startup—hooks are pure shell.

## Setup

Run `envy shell <shell>` to get the source line for your profile:

```bash
# Bash
envy shell bash    # → add to ~/.bashrc

# Zsh
envy shell zsh     # → add to ~/.zshrc

# Fish
envy shell fish    # → add to ~/.config/fish/config.fish

# PowerShell
envy shell powershell  # → add to $PROFILE
```

Hook files live at `$CACHE/shell/hook.{bash,zsh,fish,ps1}` and are written automatically during self-deploy. `envy shell` just prints the `source` line. If the cache is in a non-default location, envy warns that moving or deleting the cache will break shell integration.

## Behavior

On every directory change:

1. Walk up from `$PWD` looking for `envy.lua`
2. Respect `@envy root "false"` (continue upward) vs default `root=true` (stop)
3. Parse `@envy bin` from first 20 lines of manifest
4. Compute absolute bin path: `$manifest_dir/$bin_value`
5. Manage `PATH`—prepend new bin dir, remove old one on project switch or exit
6. Export `ENVY_PROJECT_ROOT` (or unset when leaving)

## Auto-Update

Hook files carry `_ENVY_HOOK_VERSION=N`. Any envy command checks the stamp and refreshes stale hooks automatically. Restart your shell after an update.

## Troubleshooting

**Verify hook is loaded:** `type _envy_hook` (bash/zsh) or `functions _envy_hook` (fish).

**Temporarily disable:** `export ENVY_SHELL_HOOK_DISABLE=1` (unset to re-enable).

**Force refresh:** Delete `$CACHE/shell/` and run any envy command.
