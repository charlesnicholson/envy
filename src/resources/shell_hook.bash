# envy shell hook — managed by envy; do not edit
_ENVY_HOOK_VERSION=4

# Detect UTF-8 locale for emoji/unicode output
case "${LC_ALL:-${LC_CTYPE:-${LANG:-}}}" in
  *[Uu][Tt][Ff]-8*|*[Uu][Tt][Ff]8*) _ENVY_UTF8=1; _ENVY_DASH="—" ;;
  *) _ENVY_UTF8=; _ENVY_DASH="--" ;;
esac

_envy_find_manifest() {
  local d="$PWD"
  while [ "$d" != / ]; do
    if [ -f "$d/envy.lua" ]; then
      local is_root="true"
      if head -20 "$d/envy.lua" | grep -qE '^--[[:space:]]*@envy[[:space:]]+root[[:space:]]+"false"'; then
        is_root="false"
      fi
      if [ "$is_root" = "true" ]; then
        echo "$d"
        return 0
      fi
    fi
    d="${d%/*}"
    d="${d:-/}"
  done
  return 1
}

_envy_parse_bin() {
  local manifest="$1/envy.lua"
  local bin_val
  bin_val=$(head -20 "$manifest" | sed -nE 's/^--[[:space:]]*@envy[[:space:]]+bin[[:space:]]+"(([^"\\]|\\.)*)".*/\1/p') || true
  if [ -n "$bin_val" ]; then echo "$bin_val"; fi
}

_envy_remove_from_path() {
  local remove="$1"
  local IFS=':'
  local new_path=""
  # shellcheck disable=SC2086
  for p in $PATH; do
    if [ "$p" = "$remove" ]; then continue; fi
    new_path="${new_path:+$new_path:}$p"
  done
  echo "$new_path"
}

_envy_set_prompt() {
  if [ "${ENVY_NO_PROMPT:-}" = "1" ]; then return; fi
  if [ "${_ENVY_UTF8:-}" != "1" ]; then return; fi
  if [ "${_ENVY_PROMPT_ACTIVE:-}" = "1" ]; then return; fi
  _ENVY_ORIG_PS1="$PS1"
  PS1="🦝 $PS1"
  _ENVY_PROMPT_ACTIVE=1
}

_envy_unset_prompt() {
  if [ "${_ENVY_PROMPT_ACTIVE:-}" != "1" ]; then return; fi
  PS1="$_ENVY_ORIG_PS1"
  unset _ENVY_ORIG_PS1
  unset _ENVY_PROMPT_ACTIVE
}

_envy_hook() {
  if [ "${ENVY_SHELL_HOOK_DISABLE:-}" = "1" ]; then return; fi
  if [ "$PWD" = "${_ENVY_LAST_PWD:-}" ]; then return; fi
  _ENVY_LAST_PWD="$PWD"

  local manifest_dir
  manifest_dir=$(_envy_find_manifest 2>/dev/null) || true

  if [ -n "$manifest_dir" ]; then
    local bin_val
    bin_val=$(_envy_parse_bin "$manifest_dir")
    if [ -n "$bin_val" ]; then
      local bin_dir
      bin_dir="$(cd "$manifest_dir/$bin_val" 2>/dev/null && pwd)" || true
      if [ -n "$bin_dir" ]; then
        if [ "$bin_dir" != "${_ENVY_BIN_DIR:-}" ]; then
          # Leaving old project (switching)?
          if [ -n "${_ENVY_BIN_DIR:-}" ]; then
            printf 'envy: leaving %s %s PATH restored\n' "${ENVY_PROJECT_ROOT##*/}" "$_ENVY_DASH" >&2
            PATH=$(_envy_remove_from_path "$_ENVY_BIN_DIR")
          fi
          PATH="$bin_dir:$PATH"
          export PATH
          _ENVY_BIN_DIR="$bin_dir"
          printf 'envy: entering %s %s tools added to PATH\n' "${manifest_dir##*/}" "$_ENVY_DASH" >&2
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
    printf 'envy: leaving %s %s PATH restored\n' "${ENVY_PROJECT_ROOT##*/}" "$_ENVY_DASH" >&2
    PATH=$(_envy_remove_from_path "$_ENVY_BIN_DIR")
    export PATH
    unset _ENVY_BIN_DIR
    _envy_unset_prompt
  fi
  unset ENVY_PROJECT_ROOT
}

# Register via PROMPT_COMMAND
if [[ "${PROMPT_COMMAND:-}" != *"_envy_hook"* ]]; then
  PROMPT_COMMAND="_envy_hook${PROMPT_COMMAND:+;$PROMPT_COMMAND}"
fi

# Activate for current directory
_envy_hook
