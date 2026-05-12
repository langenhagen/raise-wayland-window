# Raise Wayland Window
Bring an arbitrary window to the foreground on GNOME/Mutter/Wayland from the command line.
No XWayland. No `wmctrl`.

`raise-wayland-window` dispatches to whichever D-Bus method is appropriate for
the target: stock `gnome-shell` interfaces for cooperating apps, and the
[`run-or-raise`](https://github.com/CZ-NIC/run-or-raise) GNOME Shell extension
for raising by `wm_class` or window title.

Bash and C implementations have identical CLI and exit codes.

## Project Structure
```text
.
├── LICENSE             License file.
├── Makefile            Build/check shortcuts.
├── README.md           You are here.
├── raise-window        Compiled C binary.
├── raise-window.c      C source.
└── raise-window.sh     Bash equivalent.
```

## Dependencies
The bash script needs only `gdbus`, which ships with GNOME.

For the C build on Ubuntu:
```bash
sudo apt install build-essential pkg-config libglib2.0-dev
```
`libglib2.0-dev` provides the `gio-2.0` pkg-config module that the Makefile
asks for; `gio-2.0` is GNOME's I/O library and the source of the GDBus client
API the C code uses.

## Prerequisite: run-or-raise
The `--app-id` and `--gtk-app` selectors work on stock GNOME with no extra
setup. The `--wm-class`, `--title-regex`, and `--run-or-raise-line` selectors
call into the [`run-or-raise`](https://github.com/CZ-NIC/run-or-raise) GNOME
Shell extension; install it from extensions.gnome.org if you want those.

run-or-raise's D-Bus interface is gated behind a setting that defaults to off.
Enable it once:
```bash
gsettings \
    --schemadir ~/.local/share/gnome-shell/extensions/run-or-raise@edvard.cz/schemas \
    set org.gnome.shell.extensions.run-or-raise dbus true

gnome-extensions disable run-or-raise@edvard.cz
gnome-extensions enable  run-or-raise@edvard.cz
```

Verify:
```bash
gdbus introspect --session --dest org.gnome.Shell \
    --object-path /org/gnome/Shell/Extensions/RunOrRaise
```
The output must list `method Call(s) -> s`. An empty node means the setting
is not active yet.

## Build
```bash
make
```
Produces `./raise-window`. The bash script `raise-window.sh` has no build step.

## Usage
```text
raise-window.sh                     <hint>          # implicit --auto
raise-window.sh --auto              <hint>
raise-window.sh --app-id            <desktop-id>
raise-window.sh --gtk-app           <bus-name>
raise-window.sh --wm-class          <wm_class>
raise-window.sh --title-regex       <regex>
raise-window.sh --run-or-raise-line <raw-line>
```
The compiled `./raise-window` accepts the same arguments.

### Selectors
| selector              | mechanism                                          |
| --------------------- | -------------------------------------------------- |
| `--app-id`            | `org.gnome.Shell.FocusApp` on a `.desktop` id      |
| `--gtk-app`           | `org.freedesktop.Application.Activate`             |
| `--wm-class`          | run-or-raise `Call(",,X,")`                        |
| `--title-regex`       | run-or-raise `Call(",,,/X/")`                      |
| `--run-or-raise-line` | run-or-raise `Call(X)` verbatim                    |
| `--auto` / (default)  | `--app-id` -> `--gtk-app` -> `--wm-class` in order |

### Examples
```bash
raise-window.sh firefox
raise-window.sh --app-id org.gnome.Nautilus.desktop
raise-window.sh --gtk-app org.gnome.Nautilus
raise-window.sh --wm-class sublime_text
raise-window.sh --title-regex 'Inbox'
raise-window.sh --run-or-raise-line ',,Pidgin,/^((?!Buddy List).)*$/'
```

### Exit Codes
```text
0 ok    2 usage    3 unsupported    4 dbus unreachable    5 target not found
```


## Why
Wayland by design does not let one client manipulate another client's
surfaces. The only process with that authority on GNOME is `gnome-shell`
itself. `FocusApp` and `Application.Activate` are the cases stock GNOME
exposes to external callers; everything else (raise by `wm_class`, by title)
requires code running inside `gnome-shell`. This tool reuses the run-or-raise
extension for that, rather than shipping its own.

## License
See [LICENSE](LICENSE) file.
