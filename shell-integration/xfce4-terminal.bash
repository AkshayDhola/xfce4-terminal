# xfce4-terminal shell integration for bash
#
# Tells xfce4-terminal which command was run and whether it succeeded, so that
# the terminal can offer inline suggestions built from previously successful
# commands. Source it from ~/.bashrc:
#
#   [ -f /usr/share/xfce4-terminal/shell-integration/xfce4-terminal.bash ] && \
#     . /usr/share/xfce4-terminal/shell-integration/xfce4-terminal.bash
#
# It does nothing unless it is running in an interactive bash inside an
# xfce4-terminal that has command suggestions enabled.

case $- in
  *i*) ;;
  *) return 0 ;;
esac

[ -n "$BASH_VERSION" ] || return 0
[ -n "$XFCE4_TERMINAL_SHELL_INTEGRATION" ] || return 0

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

__xfceterm_precmd ()
{
  local status=$?
  local entry

  # nothing was run since the last prompt, so there is nothing to report
  [ "$HISTCMD" = "$__xfceterm_histcmd" ] && return 0

  if [ -n "$__xfceterm_histcmd" ]; then
    entry=$(HISTTIMEFORMAT= builtin history 1)
    if [[ $entry =~ ^[[:space:]]*[0-9]+[[:space:]]+(.*)$ ]]; then
      __xfceterm_escape "${BASH_REMATCH[1]}"
      printf '\033]666;vte.ext.xfceterm.command=%s\033\\' "$__xfceterm_escaped"
      printf '\033]666;vte.shell.postexec=%d\033\\' "$status"
    fi
  fi

  __xfceterm_histcmd=$HISTCMD
}

# Marks the spot where typing starts, so the terminal knows what has been typed.
# The terminal is only told about a termprop whose value changed, hence the
# unset right before the set: without it only the very first prompt is marked.
case $PS1 in
  *xfceterm.prompt*) ;;
  *) PS1=$PS1'\[\033]666;vte.ext.xfceterm.prompt\033\\\033]666;vte.ext.xfceterm.prompt=1\033\\\]' ;;
esac

case $PROMPT_COMMAND in
  *__xfceterm_precmd*) ;;
  "") PROMPT_COMMAND=__xfceterm_precmd ;;
  *) PROMPT_COMMAND="__xfceterm_precmd;$PROMPT_COMMAND" ;;
esac
