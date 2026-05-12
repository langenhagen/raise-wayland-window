#!/usr/bin/env bash
# raise-window.sh - raise a window on GNOME Wayland via D-Bus.
#
# No XWayland. No wmctrl. On strict Wayland the compositor is the only
# process that can raise another client's window, so this tool just makes
# the right D-Bus calls:
#
#   --app-id            org.gnome.Shell.FocusApp                (no extension)
#   --gtk-app           org.freedesktop.Application.Activate    (cooperative app)
#   --wm-class          run-or-raise extension Call(",,X,")
#   --title-regex       run-or-raise extension Call(",,,/X/")
#   --run-or-raise-line run-or-raise extension Call(X)
#   --auto / default    try app-id, gtk-app, wm-class in order

set -euo pipefail

readonly shell_dest='org.gnome.Shell'
readonly shell_path='/org/gnome/Shell'
readonly shell_focus_method='org.gnome.Shell.FocusApp'

readonly ror_path='/org/gnome/Shell/Extensions/RunOrRaise'
readonly ror_method='org.gnome.Shell.Extensions.RunOrRaise.Call'

readonly gtk_method='org.freedesktop.Application.Activate'

usage() {
    cat <<'EOF'
usage: raise-window.sh [SELECTOR] ARG

selectors:
  (none)                   same as --auto
  --auto              HINT try --app-id HINT.desktop, then --gtk-app
                           org.<HINT>.<HINT-capitalized>, then --wm-class HINT
  --app-id            ID   gnome-shell FocusApp on a .desktop id
  --gtk-app           NAME org.freedesktop.Application.Activate on a D-Bus name
  --wm-class          CLS  raise by wm_class via run-or-raise
  --title-regex       RE   raise by title regex via run-or-raise
  --run-or-raise-line STR  raw run-or-raise line, passed verbatim to Call()
  -h | --help              show this help

exit codes:
  0 ok    2 usage    3 unsupported    4 dbus unreachable    5 target not found
EOF
}

warn() {
    printf 'raise-window.sh: %s\n' "$*" >&2
}

# call_focus_app DESKTOP_ID -> 0 on success, 5 if app unknown, 4 if dbus error
call_focus_app() {
    local id=$1 out rc
    out=$(gdbus call --session --dest "$shell_dest" \
        --object-path "$shell_path" \
        --method "$shell_focus_method" "$id" 2>&1) && rc=0 || rc=$?
    if [[ $rc -eq 0 ]]; then
        return 0
    fi
    if [[ $out == *"ServiceUnknown"* || $out == *"NoReply"* ]]; then
        return 4
    fi
    return 5
}

# call_gtk_app BUS_NAME -> 0 success, 5 if name has no owner / no Activate
call_gtk_app() {
    local name=$1 path out rc
    path="/${name//.//}"
    out=$(gdbus call --session --dest "$name" \
        --object-path "$path" \
        --method "$gtk_method" '{}' 2>&1) && rc=0 || rc=$?
    if [[ $rc -eq 0 ]]; then
        return 0
    fi
    if [[ $out == *"ServiceUnknown"* || $out == *"NameHasNoOwner"* ]]; then
        return 5
    fi
    return 4
}

# call_ror LINE -> 0 success, 4 if endpoint missing, 5 if no match (best-effort)
call_ror() {
    local line=$1 out rc
    out=$(gdbus call --session --dest "$shell_dest" \
        --object-path "$ror_path" \
        --method "$ror_method" "$line" 2>&1) && rc=0 || rc=$?
    if [[ $rc -ne 0 ]]; then
        if [[ $out == *"UnknownMethod"* || $out == *"UnknownObject"* || $out == *"UnknownInterface"* ]]; then
            return 4
        fi
        return 4
    fi
    # response is ('Success',) on success; the extension always returns "Success"
    # even when no window matched, so we cannot distinguish here. Treat as ok.
    if [[ $out == *"Success"* ]]; then
        return 0
    fi
    return 5
}

dispatch_auto() {
    local hint=$1
    local cap="${hint^}"
    local guessed_bus="org.${hint}.${cap}"
    local first_fail=

    if call_focus_app "${hint}.desktop"; then
        return 0
    fi
    first_fail="FocusApp '${hint}.desktop'"

    if call_gtk_app "$guessed_bus"; then
        warn "fell back to gtk-app (${first_fail} failed)"
        return 0
    fi

    if call_ror ",,${hint},"; then
        warn "fell back to run-or-raise --wm-class (${first_fail} and gtk-app ${guessed_bus} failed)"
        return 0
    fi

    warn "no method could raise '${hint}' (tried FocusApp, Application.Activate, run-or-raise)"
    return 5
}

main() {
    if [[ $# -eq 0 ]]; then
        usage >&2
        exit 2
    fi

    case $1 in
    -h | --help)
        usage
        exit 0
        ;;
    --auto)
        [[ $# -eq 2 ]] || {
            usage >&2
            exit 2
        }
        dispatch_auto "$2"
        ;;
    --app-id)
        [[ $# -eq 2 ]] || {
            usage >&2
            exit 2
        }
        call_focus_app "$2"
        ;;
    --gtk-app)
        [[ $# -eq 2 ]] || {
            usage >&2
            exit 2
        }
        call_gtk_app "$2"
        ;;
    --wm-class)
        [[ $# -eq 2 ]] || {
            usage >&2
            exit 2
        }
        call_ror ",,$2,"
        ;;
    --title-regex)
        [[ $# -eq 2 ]] || {
            usage >&2
            exit 2
        }
        call_ror ",,,/$2/"
        ;;
    --run-or-raise-line)
        [[ $# -eq 2 ]] || {
            usage >&2
            exit 2
        }
        call_ror "$2"
        ;;
    --*)
        usage >&2
        exit 2
        ;;
    *)
        [[ $# -eq 1 ]] || {
            usage >&2
            exit 2
        }
        dispatch_auto "$1"
        ;;
    esac
}

main "$@"
