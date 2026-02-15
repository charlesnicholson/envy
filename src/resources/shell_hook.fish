# envy shell hook — managed by envy; do not edit
set -g _ENVY_HOOK_VERSION 4

# Detect UTF-8 locale for emoji/unicode output
if string match -qi '*utf-8*' -- $LC_ALL $LC_CTYPE $LANG
    set -g _ENVY_UTF8 1
    set -g _ENVY_DASH "—"
else
    set -g _ENVY_UTF8 0
    set -g _ENVY_DASH "--"
end

function _envy_find_manifest
    set -l d $PWD
    while test "$d" != /
        if test -f "$d/envy.lua"
            set -l is_root true
            if head -20 "$d/envy.lua" | grep -qE '^--[[:space:]]*@envy[[:space:]]+root[[:space:]]+"false"'
                set is_root false
            end
            if test "$is_root" = true
                echo "$d"
                return 0
            end
        end
        set d (string replace -r '/[^/]*$' '' "$d")
        test -z "$d"; and set d /
    end
    return 1
end

function _envy_parse_bin
    set -l manifest "$argv[1]/envy.lua"
    set -l bin_val (head -20 "$manifest" | sed -nE 's/^--[[:space:]]*@envy[[:space:]]+bin[[:space:]]+"(([^"\\\\]|\\\\.)*)".*/\\1/p')
    test -n "$bin_val"; and echo "$bin_val"
end

function _envy_set_prompt
    test "$ENVY_NO_PROMPT" = 1; and return
    test "$_ENVY_UTF8" != 1; and return
    test "$_ENVY_PROMPT_ACTIVE" = 1; and return
    if functions -q fish_prompt; and not functions -q _envy_original_fish_prompt
        functions -c fish_prompt _envy_original_fish_prompt
        function fish_prompt
            printf '🦝 '
            _envy_original_fish_prompt
        end
        set -g _ENVY_PROMPT_ACTIVE 1
    end
end

function _envy_unset_prompt
    test "$_ENVY_PROMPT_ACTIVE" != 1; and return
    if functions -q _envy_original_fish_prompt
        functions -e fish_prompt
        functions -c _envy_original_fish_prompt fish_prompt
        functions -e _envy_original_fish_prompt
    end
    set -e _ENVY_PROMPT_ACTIVE
end

function _envy_hook --on-variable PWD
    test "$ENVY_SHELL_HOOK_DISABLE" = 1; and return
    # Deduplicate: --on-variable PWD can fire from cd in command substitutions
    test "$PWD" = "$_ENVY_LAST_PWD"; and return
    set -g _ENVY_LAST_PWD "$PWD"

    set -l manifest_dir (_envy_find_manifest 2>/dev/null)

    if test -n "$manifest_dir"
        set -l bin_val (_envy_parse_bin "$manifest_dir")
        if test -n "$bin_val"
            set -l bin_dir (realpath "$manifest_dir/$bin_val" 2>/dev/null)
            if test -n "$bin_dir"
                if test "$bin_dir" != "$_ENVY_BIN_DIR"
                    # Leaving old project (switching)?
                    if set -q _ENVY_BIN_DIR
                        echo "envy: leaving "(string replace -r '.*/' '' "$ENVY_PROJECT_ROOT")" $_ENVY_DASH PATH restored" >&2
                        set -l idx (contains -i -- "$_ENVY_BIN_DIR" $PATH)
                        if test -n "$idx"
                            set -e PATH[$idx]
                        end
                    end
                    set -gx PATH "$bin_dir" $PATH
                    set -g _ENVY_BIN_DIR "$bin_dir"
                    echo "envy: entering "(string replace -r '.*/' '' "$manifest_dir")" $_ENVY_DASH tools added to PATH" >&2
                    _envy_set_prompt
                end
                set -gx ENVY_PROJECT_ROOT "$manifest_dir"
                return
            end
        end
    end

    # Left all projects or no bin — clean up
    if set -q _ENVY_BIN_DIR
        echo "envy: leaving "(string replace -r '.*/' '' "$ENVY_PROJECT_ROOT")" $_ENVY_DASH PATH restored" >&2
        set -l idx (contains -i -- "$_ENVY_BIN_DIR" $PATH)
        if test -n "$idx"
            set -e PATH[$idx]
        end
        set -e _ENVY_BIN_DIR
        _envy_unset_prompt
    end
    set -e ENVY_PROJECT_ROOT
end

# Activate for current directory
_envy_hook
