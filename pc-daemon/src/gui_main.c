/*
 * Simple GTK3 front-end for micify-daemon. Deliberately thin: it launches
 * the already-verified CLI daemon as a subprocess and reflects its status,
 * rather than re-implementing the audio pipeline in the GUI process. The
 * daemon binary stays fully usable headless/standalone either way.
 */
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <unistd.h>
#include <string.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *port_entry;
    GtkWidget *usb_check;
    GtkWidget *start_button;
    GtkWidget *status_label;
    GSubprocess *daemon_proc;
} AppState;

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

static void set_status(AppState *app, const char *text) {
    gtk_label_set_text(GTK_LABEL(app->status_label), text);
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
        if (g_strstr_len(data, (gssize) size, "session started")) {
            set_status(app, "Streaming - phone connected");
        } else if (g_strstr_len(data, (gssize) size, "listening on")) {
            set_status(app, "Waiting for phone to connect...");
        } else if (g_strstr_len(data, (gssize) size, "re-announced")) {
            set_status(app, "Streaming - phone reconnected");
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
    gtk_button_set_label(GTK_BUTTON(app->start_button), "Start streaming");
    set_status(app, "Idle");
}

static void on_start_stop_clicked(GtkButton *button, gpointer user_data) {
    AppState *app = (AppState *) user_data;
    (void) button;

    if (app->daemon_proc) {
        stop_daemon(app);
        return;
    }

    const char *port_text = gtk_entry_get_text(GTK_ENTRY(app->port_entry));
    gboolean usb = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->usb_check));

    gchar *daemon_path = find_daemon_path();
    GError *error = NULL;

    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);

    if (usb) {
        app->daemon_proc = g_subprocess_launcher_spawn(launcher, &error, daemon_path,
                                                        "--port", port_text, "--usb", NULL);
    } else {
        app->daemon_proc = g_subprocess_launcher_spawn(launcher, &error, daemon_path,
                                                        "--port", port_text, NULL);
    }
    g_object_unref(launcher);
    g_free(daemon_path);

    if (!app->daemon_proc) {
        gchar *msg = g_strdup_printf("Failed to start: %s", error ? error->message : "unknown error");
        set_status(app, msg);
        g_free(msg);
        if (error) g_error_free(error);
        return;
    }

    gtk_button_set_label(GTK_BUTTON(app->start_button), "Stop");
    set_status(app, "Starting...");
    start_reading(app, g_subprocess_get_stdout_pipe(app->daemon_proc));
}

static gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    (void) widget;
    (void) event;
    AppState *app = (AppState *) user_data;
    stop_daemon(app);
    gtk_main_quit();
    return FALSE;
}

static void activate(GtkApplication *gtk_app, gpointer user_data) {
    (void) user_data;
    AppState *app = g_new0(AppState, 1);

    app->window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(app->window), "Micify");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 340, 220);
    gtk_container_set_border_width(GTK_CONTAINER(app->window), 16);

    gchar *dir = self_dir();
    gchar *icon_path = g_build_filename(dir, "micify_icon.png", NULL);
    gtk_window_set_icon_from_file(GTK_WINDOW(app->window), icon_path, NULL);
    g_free(icon_path);
    g_free(dir);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_add(GTK_CONTAINER(app->window), grid);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span size='large' weight='bold'>Micify</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), title, 0, 0, 2, 1);

    GtkWidget *port_label = gtk_label_new("Port:");
    gtk_widget_set_halign(port_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), port_label, 0, 1, 1, 1);

    app->port_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(app->port_entry), "44551");
    gtk_grid_attach(GTK_GRID(grid), app->port_entry, 1, 1, 1, 1);

    app->usb_check = gtk_check_button_new_with_label("USB mode (adb forward)");
    gtk_grid_attach(GTK_GRID(grid), app->usb_check, 0, 2, 2, 1);

    app->start_button = gtk_button_new_with_label("Start streaming");
    g_signal_connect(app->start_button, "clicked", G_CALLBACK(on_start_stop_clicked), app);
    gtk_grid_attach(GTK_GRID(grid), app->start_button, 0, 3, 2, 1);

    app->status_label = gtk_label_new("Idle");
    gtk_widget_set_halign(app->status_label, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(app->status_label), TRUE);
    gtk_grid_attach(GTK_GRID(grid), app->status_label, 0, 4, 2, 1);

    g_signal_connect(app->window, "delete-event", G_CALLBACK(on_window_delete), app);

    gtk_widget_show_all(app->window);
}

int main(int argc, char **argv) {
    GtkApplication *gtk_app = gtk_application_new("com.micify.app.gui", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(gtk_app), argc, argv);
    g_object_unref(gtk_app);
    return status;
}
