# envy shell hook — managed by envy; do not edit
_ENVY_HOOK_VERSION=@@ENVY_HOOK_VERSION@@

# Detect UTF-8 locale for emoji/unicode output
case "${LC_ALL:-${LC_CTYPE:-${LANG:-}}}" in
  *[Uu][Tt][Ff]-8*|*[Uu][Tt][Ff]8*) _ENVY_UTF8=1; _ENVY_DASH="—" ;;
  *) _ENVY_UTF8=; _ENVY_DASH="--" ;;
esac

# Prompt prefix: raccoon emoji wrapped in %{...%2G%} so zsh skips its
# own width measurement and uses the explicit 2-column declaration.
_ENVY_PROMPT_PREFIX="%{🦝%2G%} "

# p10k custom segment — called by p10k if registered; harmless when p10k absent.
prompt_envy() {
  emulate -L zsh
  [[ "${_ENVY_PROMPT_ACTIVE:-}" = "1" ]] || return
  p10k segment -f 208 -t '🦝'
}

_envy_find_manifest() {
  emulate -L zsh
  REPLY=""
  local d="$PWD"
  while [ "$d" != / ]; do
    if [ -f "$d/envy.lua" ]; then
      local is_root="true"
      # Pure zsh read+regex instead of head|grep — avoids fork/exec per directory
      local line i=0
      while (( i++ < 20 )) && IFS= read -r line; do
        if [[ "$line" =~ '^--[[:space:]]*@envy[[:space:]]+root[[:space:]]+"false"' ]]; then
          is_root="false"
          break
        fi
      done < "$d/envy.lua"
      if [ "$is_root" = "true" ]; then
        # Return via REPLY to avoid $() subshell at call site
        REPLY="$d"
        return 0
      fi
    fi
    d="${d%/*}"
    d="${d:-/}"
  done
  return 1
}

_envy_parse_bin() {
  emulate -L zsh
  local manifest="$1/envy.lua"
  # Pure zsh read+regex instead of head|sed — avoids fork/exec; returns via REPLY
  local line i=0
  REPLY=""
  while (( i++ < 20 )) && IFS= read -r line; do
    if [[ "$line" =~ '^--[[:space:]]*@envy[[:space:]]+bin[[:space:]]+"(([^"\\]|\\.)*)"' ]]; then
      REPLY="${match[1]}"
      return
    fi
  done < "$manifest"
}

_envy_remove_from_path() {
  emulate -L zsh
  # Filter zsh path array in-place — zsh auto-syncs it to $PATH.
  # Avoids the old loop+echo pattern which required a $() subshell to capture.
  path=("${(@)path:#${(b)1}}")
}

_envy_set_prompt() {
  emulate -L zsh
  if [ "${ENVY_SHELL_NO_ICON:-}" = "1" ]; then return; fi
  if [ "${_ENVY_UTF8:-}" != "1" ]; then return; fi
  if [ "${_ENVY_PROMPT_ACTIVE:-}" = "1" ]; then return; fi
  _ENVY_PROMPT_ACTIVE=1
  # p10k renders via prompt_envy() segment — skip PROMPT manipulation
  (( ${+functions[p10k]} )) && return
  PROMPT="${_ENVY_PROMPT_PREFIX}${PROMPT}"
}

_envy_unset_prompt() {
  emulate -L zsh
  if [ "${_ENVY_PROMPT_ACTIVE:-}" != "1" ]; then return; fi
  if ! (( ${+functions[p10k]} )); then
    PROMPT="${PROMPT#"${_ENVY_PROMPT_PREFIX}"}"
  fi
  unset _ENVY_PROMPT_ACTIVE
}

# Runs before each prompt: re-applies raccoon if a theme overwrote PROMPT
_envy_precmd() {
  emulate -L zsh
  if [ "${ENVY_SHELL_NO_ICON:-}" = "1" ] || [ "${_ENVY_UTF8:-}" != "1" ]; then
    _envy_unset_prompt
    return
  fi
  if [ "${_ENVY_PROMPT_ACTIVE:-}" != "1" ]; then return; fi
  # p10k renders via prompt_envy() segment — no PROMPT fixup needed
  (( ${+functions[p10k]} )) && return
  if [[ "$PROMPT" != "${_ENVY_PROMPT_PREFIX}"* ]]; then
    PROMPT="${_ENVY_PROMPT_PREFIX}${PROMPT}"
    # Another precmd overwrote PROMPT — ensure we run last next time
    if [[ "${precmd_functions[-1]}" != "_envy_precmd" ]]; then
      precmd_functions=("${(@)precmd_functions:#_envy_precmd}" _envy_precmd)
    fi
  fi
}

_envy_hook() {
  emulate -L zsh
  if [ "${ENVY_SHELL_HOOK_DISABLE:-}" = "1" ]; then return; fi
  # Guard against chpwd recursion: $(cd ...) in subshells inherits this local
  if [ -n "${_ENVY_HOOK_ACTIVE:-}" ]; then return; fi
  local _ENVY_HOOK_ACTIVE=1

  # Call helpers directly instead of via $() — each $() forks a subshell,
  # and macOS fork() costs ~1-2ms each. Results come back through REPLY.
  local manifest_dir bin_val bin_dir
  if _envy_find_manifest 2>/dev/null; then
    manifest_dir="$REPLY"
    _envy_parse_bin "$manifest_dir"
    bin_val="$REPLY"
    if [ -n "$bin_val" ]; then
      bin_dir="${manifest_dir}/${bin_val}"
      # Resolve to real path using zsh :A modifier (no subshell)
      bin_dir="${bin_dir:A}"
      if [ -d "$bin_dir" ]; then
        if [ "$bin_dir" != "${_ENVY_BIN_DIR:-}" ]; then
          # Leaving old project (switching)?
          if [ -n "${_ENVY_BIN_DIR:-}" ]; then
            if [ "${ENVY_SHELL_NO_ENTER_EXIT_ANNOUNCE:-}" != "1" ]; then
              printf 'envy: leaving %s %s PATH restored\n' "${ENVY_PROJECT_ROOT##*/}" "$_ENVY_DASH" >&2
            fi
            _envy_remove_from_path "$_ENVY_BIN_DIR"
          fi
          path=("$bin_dir" "${path[@]}")  # prepend via zsh path array — no subshell
          export PATH
          _ENVY_BIN_DIR="$bin_dir"
          if [ "${ENVY_SHELL_NO_ENTER_EXIT_ANNOUNCE:-}" != "1" ]; then
            printf 'envy: entering %s %s tools added to PATH\n' "${manifest_dir##*/}" "$_ENVY_DASH" >&2
          fi
          _envy_set_prompt
        fi
        ENVY_PROJECT_ROOT="$manifest_dir"
        export ENVY_PROJECT_ROOT
        return
      fi
    fi
  fi

  # Left all projects or no bin — clean up
  if [ -n "${_ENVY_BIN_DIR:-}" ]; then
    if [ "${ENVY_SHELL_NO_ENTER_EXIT_ANNOUNCE:-}" != "1" ]; then
      printf 'envy: leaving %s %s PATH restored\n' "${ENVY_PROJECT_ROOT##*/}" "$_ENVY_DASH" >&2
    fi
    _envy_remove_from_path "$_ENVY_BIN_DIR"
    export PATH
    unset _ENVY_BIN_DIR
    _envy_unset_prompt
  fi
  unset ENVY_PROJECT_ROOT
}

# Register via chpwd (fires only on directory change — more efficient than precmd)
if [[ -z "${chpwd_functions[(r)_envy_hook]}" ]]; then
  chpwd_functions+=(_envy_hook)
fi
if [[ -z "${precmd_functions[(r)_envy_precmd]}" ]]; then
  precmd_functions+=(_envy_precmd)
fi

# Auto-register p10k segment if available (raccoon appears in p10k's prompt)
if [ "${ENVY_SHELL_NO_ICON:-}" != "1" ] && [ "${_ENVY_UTF8:-}" = "1" ] && \
   (( ${+functions[p10k]} )) && [[ -n "${POWERLEVEL9K_LEFT_PROMPT_ELEMENTS+x}" ]]; then
  if [[ -z "${POWERLEVEL9K_LEFT_PROMPT_ELEMENTS[(r)envy]}" ]]; then
    POWERLEVEL9K_LEFT_PROMPT_ELEMENTS=(envy "${POWERLEVEL9K_LEFT_PROMPT_ELEMENTS[@]}")
    p10k reload 2>/dev/null || true
  fi
fi

# Activate for current directory
_envy_hook
