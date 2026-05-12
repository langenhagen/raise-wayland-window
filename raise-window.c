/*
 * raise-window - raise a window on GNOME Wayland via D-Bus.
 *
 * C equivalent of the raise-window bash script. Same CLI, same exit codes,
 * same logging contract (one stderr line only when --auto falls back or
 * exhausts all methods). Uses GIO's GDBusConnection directly so the only
 * runtime dependency is glib/gio.
 *
 * Build:
 *   gcc -O2 -Wall -Wextra -o raise-window raise-window.c \
 *       $(pkg-config --cflags --libs gio-2.0)
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>

#define SHELL_DEST    "org.gnome.Shell"
#define SHELL_PATH    "/org/gnome/Shell"
#define SHELL_IFACE   "org.gnome.Shell"
#define FOCUS_METHOD  "FocusApp"

#define ROR_PATH      "/org/gnome/Shell/Extensions/RunOrRaise"
#define ROR_IFACE     "org.gnome.Shell.Extensions.RunOrRaise"
#define ROR_METHOD    "Call"

#define GTK_IFACE     "org.freedesktop.Application"
#define GTK_METHOD    "Activate"

enum {
    RC_OK              = 0,
    RC_USAGE           = 2,
    RC_UNSUPPORTED     = 3,
    RC_DBUS_UNREACH    = 4,
    RC_TARGET_NOT_FOUND = 5,
};

static void usage(FILE *out)
{
    fputs(
        "usage: raise-window [SELECTOR] ARG\n"
        "\n"
        "selectors:\n"
        "  (none)                   same as --auto\n"
        "  --auto              HINT try --app-id HINT.desktop, then --gtk-app\n"
        "                           org.<HINT>.<HINT-capitalized>, then --wm-class HINT\n"
        "  --app-id            ID   gnome-shell FocusApp on a .desktop id\n"
        "  --gtk-app           NAME org.freedesktop.Application.Activate on a D-Bus name\n"
        "  --wm-class          CLS  raise by wm_class via run-or-raise\n"
        "  --title-regex       RE   raise by title regex via run-or-raise\n"
        "  --run-or-raise-line STR  raw run-or-raise line, passed verbatim to Call()\n"
        "  -h | --help              show this help\n"
        "\n"
        "exit codes:\n"
        "  0 ok    2 usage    3 unsupported    4 dbus unreachable    5 target not found\n",
        out);
}

static void warn_user(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("raise-window: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

/* Return RC_OK / RC_DBUS_UNREACH / RC_TARGET_NOT_FOUND based on error. */
static int classify_error(GError *error)
{
    if (!error)
        return RC_OK;

    const char *remote = g_dbus_error_get_remote_error(error);
    if (remote) {
        if (strstr(remote, "ServiceUnknown") ||
            strstr(remote, "NameHasNoOwner") ||
            strstr(remote, "UnknownObject") ||
            strstr(remote, "UnknownInterface") ||
            strstr(remote, "UnknownMethod"))
            return RC_DBUS_UNREACH;
    }
    if (error->message &&
        (strstr(error->message, "ServiceUnknown") ||
         strstr(error->message, "NameHasNoOwner")))
        return RC_DBUS_UNREACH;

    return RC_TARGET_NOT_FOUND;
}

static int call_focus_app(GDBusConnection *bus, const char *desktop_id)
{
    GError *err = NULL;
    GVariant *res = g_dbus_connection_call_sync(
        bus, SHELL_DEST, SHELL_PATH, SHELL_IFACE, FOCUS_METHOD,
        g_variant_new("(s)", desktop_id),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
    if (res) {
        g_variant_unref(res);
        return RC_OK;
    }
    int rc = classify_error(err);
    g_clear_error(&err);
    return rc;
}

static int call_gtk_app(GDBusConnection *bus, const char *bus_name)
{
    if (!g_dbus_is_name(bus_name))
        return RC_TARGET_NOT_FOUND;

    /* path = "/" + bus_name with '.' -> '/' */
    size_t n = strlen(bus_name);
    char *path = g_malloc(n + 2);
    path[0] = '/';
    for (size_t i = 0; i < n; i++)
        path[i + 1] = (bus_name[i] == '.') ? '/' : bus_name[i];
    path[n + 1] = '\0';

    if (!g_variant_is_object_path(path)) {
        g_free(path);
        return RC_TARGET_NOT_FOUND;
    }

    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    GVariant *platform = g_variant_builder_end(&b);

    GError *err = NULL;
    GVariant *res = g_dbus_connection_call_sync(
        bus, bus_name, path, GTK_IFACE, GTK_METHOD,
        g_variant_new("(@a{sv})", platform),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
    g_free(path);
    if (res) {
        g_variant_unref(res);
        return RC_OK;
    }
    int rc = classify_error(err);
    g_clear_error(&err);
    return rc;
}

static int call_ror(GDBusConnection *bus, const char *line)
{
    GError *err = NULL;
    GVariant *res = g_dbus_connection_call_sync(
        bus, SHELL_DEST, ROR_PATH, ROR_IFACE, ROR_METHOD,
        g_variant_new("(s)", line),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
    if (res) {
        /* The extension always returns "Success"; we cannot detect "no match". */
        g_variant_unref(res);
        return RC_OK;
    }
    int rc = classify_error(err);
    g_clear_error(&err);
    /* Missing endpoint reported as dbus-unreachable. */
    return rc;
}

static int dispatch_auto(GDBusConnection *bus, const char *hint)
{
    size_t n = strlen(hint);

    char *desktop_id = g_strdup_printf("%s.desktop", hint);
    int r = call_focus_app(bus, desktop_id);
    g_free(desktop_id);
    if (r == RC_OK)
        return RC_OK;

    /* Guess a GTK-app bus name: org.<hint>.<Hint> with first letter capitalized. */
    char *cap = g_strdup(hint);
    if (n > 0 && cap[0] >= 'a' && cap[0] <= 'z')
        cap[0] = (char) toupper((unsigned char) cap[0]);
    char *guessed_bus = g_strdup_printf("org.%s.%s", hint, cap);
    g_free(cap);

    char *first_fail = g_strdup_printf("FocusApp '%s.desktop'", hint);

    int r2 = call_gtk_app(bus, guessed_bus);
    if (r2 == RC_OK) {
        warn_user("fell back to gtk-app (%s failed)", first_fail);
        g_free(guessed_bus);
        g_free(first_fail);
        return RC_OK;
    }

    char *ror_line = g_strdup_printf(",,%s,", hint);
    int r3 = call_ror(bus, ror_line);
    g_free(ror_line);
    if (r3 == RC_OK) {
        warn_user("fell back to run-or-raise --wm-class (%s and gtk-app %s failed)",
                  first_fail, guessed_bus);
        g_free(guessed_bus);
        g_free(first_fail);
        return RC_OK;
    }

    warn_user("no method could raise '%s' (tried FocusApp, Application.Activate, run-or-raise)",
              hint);
    g_free(guessed_bus);
    g_free(first_fail);
    return RC_TARGET_NOT_FOUND;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(stderr);
        return RC_USAGE;
    }

    if (g_strcmp0(argv[1], "-h") == 0 || g_strcmp0(argv[1], "--help") == 0) {
        usage(stdout);
        return RC_OK;
    }

    GError *err = NULL;
    GDBusConnection *bus =
        g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!bus) {
        warn_user("cannot connect to session bus: %s",
                  err ? err->message : "(unknown)");
        g_clear_error(&err);
        return RC_DBUS_UNREACH;
    }

    int rc;
    if (argv[1][0] == '-' && argv[1][1] == '-') {
        if (argc != 3) {
            usage(stderr);
            g_object_unref(bus);
            return RC_USAGE;
        }
        const char *flag = argv[1];
        const char *arg  = argv[2];

        if (g_strcmp0(flag, "--auto") == 0)
            rc = dispatch_auto(bus, arg);
        else if (g_strcmp0(flag, "--app-id") == 0)
            rc = call_focus_app(bus, arg);
        else if (g_strcmp0(flag, "--gtk-app") == 0)
            rc = call_gtk_app(bus, arg);
        else if (g_strcmp0(flag, "--wm-class") == 0) {
            char *line = g_strdup_printf(",,%s,", arg);
            rc = call_ror(bus, line);
            g_free(line);
        } else if (g_strcmp0(flag, "--title-regex") == 0) {
            char *line = g_strdup_printf(",,,/%s/", arg);
            rc = call_ror(bus, line);
            g_free(line);
        } else if (g_strcmp0(flag, "--run-or-raise-line") == 0) {
            rc = call_ror(bus, arg);
        } else {
            usage(stderr);
            rc = RC_USAGE;
        }
    } else {
        if (argc != 2) {
            usage(stderr);
            g_object_unref(bus);
            return RC_USAGE;
        }
        rc = dispatch_auto(bus, argv[1]);
    }

    g_object_unref(bus);
    return rc;
}
