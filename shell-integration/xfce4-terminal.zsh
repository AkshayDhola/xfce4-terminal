# xfce4-terminal shell integration for zsh
#
# Tells xfce4-terminal which command was run and whether it succeeded, so that
# the terminal can offer inline suggestions built from previously successful
# commands. Source it from ~/.zshrc:
#
#   [ -f /usr/share/xfce4-terminal/shell-integration/xfce4-terminal.zsh ] && \
#     . /usr/share/xfce4-terminal/shell-integration/xfce4-terminal.zsh
#
# It does nothing unless it is running in an interactive zsh inside an
# xfce4-terminal that has command suggestions enabled.

[[ -o interactive ]] || return 0
[[ -n $ZSH_VERSION ]] || return 0
[[ -n $XFCE4_TERMINAL_SHELL_INTEGRATION ]] || return 0

# The terminal reads the value of an OSC 666 termprop up to the first ';', and
# '\' introduces an escape there, so both have to be quoted. Control characters
# would end the sequence altogether and become spaces.
__xfceterm_escape ()
{
  local text=$1

  text=${text//\\/\\\\}
  text=${text//;/\\s}
  __xfceterm_escaped=${text//[[:cntrl:]]/ }
}

__xfceterm_preexec ()
{
  __xfceterm_command=$1
}

__xfceterm_precmd ()
{
  local status=$?

  if [[ -n $__xfceterm_command ]]; then
    __xfceterm_escape "$__xfceterm_command"
    printf '\033]666;vte.ext.xfceterm.command=%s\033\\' "$__xfceterm_escaped"
    printf '\033]666;vte.shell.postexec=%d\033\\' "$status"
    __xfceterm_command=
  fi
}

autoload -Uz add-zsh-hook
add-zsh-hook preexec __xfceterm_preexec
add-zsh-hook precmd __xfceterm_precmd

# Marks the spot where typing starts, so the terminal knows what has been typed.
# The terminal is only told about a termprop whose value changed, hence the
# unset right before the set: without it only the very first prompt is marked.
if [[ $PS1 != *xfceterm.prompt* ]]; then
  __xfceterm_mark=$(printf '\033]666;vte.ext.xfceterm.prompt\033\\\033]666;vte.ext.xfceterm.prompt=1\033\\')
  PS1="$PS1%{$__xfceterm_mark%}"
fi
