/*
 * Simple GTK3 front-end for micify-daemon. Deliberately thin: it launches
 * the already-verified CLI daemon as a subprocess and reflects its status,
 * rather than re-implementing the audio pipeline in the GUI process. The
 * daemon binary stays fully usable headless/standalone either way.
 *
 * Auto-starts listening the moment the window opens - there is no manual
 * "connect" step and no port to configure (fixed at MICIFY_DEFAULT_PORT,
 * matching the Android app's default). It just shows whether a phone is
 * currently connected and, if so, for how long.
 */
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#define MICIFY_DEFAULT_PORT "44551"

typedef struct {
    GtkWidget *window;
    GtkWidget *usb_check;
    GtkWidget *status_label;
    GSubprocess *daemon_proc;

    gboolean connected;
    time_t connected_since;
    guint duration_timer_id;
} AppState;

static void start_daemon(AppState *app);
static void stop_daemon(AppState *app);

static gchar *self_dir(void) {
    char exe_path[4096];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len < 0) return g_strdup(".");
    exe_path[len] = 0;
    return g_path_get_dirname(exe_path);
}

static gchar *find_daemon_path(void) {
    gchar *dir = self_dir();
    gchar *candidate = g_build_filename(dir, "micify-daemon", NULL);
    g_free(dir);
    return candidate;
}

static void render_status(AppState *app) {
    if (app->connected) {
        long elapsed = (long) difftime(time(NULL), app->connected_since);
        gchar *text = g_strdup_printf("Connected - streaming for %ld:%02ld",
                                       elapsed / 60, elapsed % 60);
        gtk_label_set_text(GTK_LABEL(app->status_label), text);
        g_free(text);
    } else {
        gtk_label_set_text(GTK_LABEL(app->status_label), "Not connected - waiting for phone...");
    }
}

static gboolean duration_tick(gpointer user_data) {
    AppState *app = (AppState *) user_data;
    if (app->connected) render_status(app);
    return G_SOURCE_CONTINUE;
}

static void set_connected(AppState *app, gboolean connected) {
    if (connected && !app->connected) {
        app->connected_since = time(NULL);
    }
    app->connected = connected;
    render_status(app);
}

static void read_daemon_output(GObject *source, GAsyncResult *res, gpointer user_data);

static void start_reading(AppState *app, GInputStream *stdout_pipe) {
    g_input_stream_read_bytes_async(stdout_pipe, 4096, G_PRIORITY_DEFAULT, NULL,
                                     read_daemon_output, app);
}

static void read_daemon_output(GObject *source, GAsyncResult *res, gpointer user_data) {
    AppState *app = user_data;
    GError *error = NULL;
    GBytes *bytes = g_input_stream_read_bytes_finish(G_INPUT_STREAM(source), res, &error);
    if (!bytes) {
        if (error) g_error_free(error);
        return;
    }

    gsize size = 0;
    const char *data = g_bytes_get_data(bytes, &size);

    if (size > 0 && app->daemon_proc) {
        if (g_strstr_len(data, (gssize) size, "session started") ||
            g_strstr_len(data, (gssize) size, "phone reconnected") ||
            g_strstr_len(data, (gssize) size, "re-announced")) {
            set_connected(app, TRUE);
        } else if (g_strstr_len(data, (gssize) size, "phone disconnected")) {
            set_connected(app, FALSE);
        }
        start_reading(app, G_INPUT_STREAM(source));
    }

    g_bytes_unref(bytes);
}

static void stop_daemon(AppState *app) {
    if (app->daemon_proc) {
        g_subprocess_send_signal(app->daemon_proc, SIGTERM);
        g_clear_object(&app->daemon_proc);
    }
    set_connected(app, FALSE);
}

static void start_daemon(AppState *app) {
    stop_daemon(app);

    gboolean usb = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->usb_check));

    gchar *daemon_path = find_daemon_path();
    GError *error = NULL;

    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);

    if (usb) {
        app->daemon_proc = g_subprocess_launcher_spawn(launcher, &error, daemon_path,
                                                        "--port", MICIFY_DEFAULT_PORT, "--usb", NULL);
    } else {
        app->daemon_proc = g_subprocess_launcher_spawn(launcher, &error, daemon_path,
                                                        "--port", MICIFY_DEFAULT_PORT, NULL);
    }
    g_object_unref(launcher);
    g_free(daemon_path);

    if (!app->daemon_proc) {
        gchar *msg = g_strdup_printf("Failed to start: %s", error ? error->message : "unknown error");
        gtk_label_set_text(GTK_LABEL(app->status_label), msg);
        g_free(msg);
        if (error) g_error_free(error);
        return;
    }

    render_status(app);
    start_reading(app, g_subprocess_get_stdout_pipe(app->daemon_proc));
}

static void on_usb_toggled(GtkWidget *widget, gpointer user_data) {
    (void) widget;
    start_daemon((AppState *) user_data);
}

static gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    (void) widget;
    (void) event;
    AppState *app = (AppState *) user_data;
    stop_daemon(app);
    if (app->duration_timer_id) g_source_remove(app->duration_timer_id);
    gtk_main_quit();
    return FALSE;
}

static void activate(GtkApplication *gtk_app, gpointer user_data) {
    (void) user_data;
    AppState *app = g_new0(AppState, 1);

    app->window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(app->window), "Micify");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 360, 160);
    gtk_container_set_border_width(GTK_CONTAINER(app->window), 16);

    gchar *dir = self_dir();
    gchar *icon_path = g_build_filename(dir, "micify_icon.png", NULL);
    gtk_window_set_icon_from_file(GTK_WINDOW(app->window), icon_path, NULL);
    g_free(icon_path);
    g_free(dir);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_add(GTK_CONTAINER(app->window), box);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span size='large' weight='bold'>Micify</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);

    app->usb_check = gtk_check_button_new_with_label("USB mode (adb forward)");
    g_signal_connect(app->usb_check, "toggled", G_CALLBACK(on_usb_toggled), app);
    gtk_box_pack_start(GTK_BOX(box), app->usb_check, FALSE, FALSE, 0);

    app->status_label = gtk_label_new("Starting...");
    gtk_widget_set_halign(app->status_label, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(app->status_label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), app->status_label, FALSE, FALSE, 0);

    g_signal_connect(app->window, "delete-event", G_CALLBACK(on_window_delete), app);

    gtk_widget_show_all(app->window);

    app->duration_timer_id = g_timeout_add_seconds(1, duration_tick, app);

    /* Auto-start immediately - no manual "connect" step, no port to type. */
    start_daemon(app);
}

int main(int argc, char **argv) {
    GtkApplication *gtk_app = gtk_application_new("com.micify.app.gui", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(gtk_app), argc, argv);
    g_object_unref(gtk_app);
    return status;
}
