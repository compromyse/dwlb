#include <stdlib.h>
#include <unistd.h>
// #include <limits.h>

#include <glib.h>
#include <gtk4-layer-shell.h>
#include <gtk/gtk.h>
// #include "glib-unix.h"

#include "dwlbtray.h"


const char *RESOURCE_PATH;


static void
activate(GtkApplication* app, StatusNotifierHost *snhost)
{
	GdkDisplay *display = gdk_display_get_default();

	GtkWindow *window = GTK_WINDOW(gtk_application_window_new(app));
	snhost->window = window;

	gtk_layer_init_for_window(window);
	gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_BOTTOM);
	gtk_layer_set_exclusive_zone(window, -1);
	static const gboolean anchors[] = {FALSE, TRUE, TRUE, FALSE};
	for (int i = 0; i < GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER; i++) {
		gtk_layer_set_anchor(window, i, anchors[i]);
	}

	if (snhost->traymon) {
		GListModel *mons = gdk_display_get_monitors(display);
		for (uint i = 0; i < g_list_model_get_n_items(mons); i++) {
			GdkMonitor *mon = g_list_model_get_item(mons, i);
			const char *conn = gdk_monitor_get_connector(mon);
			if (strcmp(conn, snhost->traymon) == 0) {
				gtk_layer_set_monitor(window, mon);
			}
		}
	}

	GtkCssProvider *css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(css, snhost->cssdata);
	gtk_style_context_add_provider_for_display(display,
	                                           GTK_STYLE_PROVIDER(css),
	                                           GTK_STYLE_PROVIDER_PRIORITY_USER);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

	gtk_widget_set_vexpand(box, TRUE);
	gtk_widget_set_hexpand(box, TRUE);
	gtk_widget_set_margin_start(box, snhost->margin);
	gtk_widget_set_margin_end(box, snhost->margin);

	gtk_window_set_default_size(GTK_WINDOW(window), 22, snhost->height);
	dwlb_request_resize(snhost);

	snhost->box = box;

	gtk_window_set_child(window, box);

	gtk_window_present(window);

}


/*
 * gboolean
 * terminate_app(StatusNotifierHost *snhost)
 * {
 * 	terminate_statusnotifierhost(snhost);
 *
 * 	GApplication *app = g_application_get_default();
 * 	g_application_release(app);
 *
 * 	return G_SOURCE_REMOVE;
 * }
 */

int
main(int argc, char *argv[])
{
	char progname[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", progname, sizeof(progname));

	if (len != -1) {
		progname[len] = '\0';
	}
	else {
		fprintf(stderr, "bad progname, exiting\n");
		exit(-1);
	}

	if (strncmp(progname, BUILD_DIR, strlen(BUILD_DIR)) == 0) {
		RESOURCE_PATH = g_strdup_printf("%s%s", BUILD_DIR, "Resources");
	} else {
		const char * const *datadirs = g_get_system_data_dirs();
		int i;

		for (i = 0; datadirs[i]; i++) {
			char *test = g_build_filename(datadirs[i], "dwlb", "boxbg.css", NULL);
			if (g_file_test(test, G_FILE_TEST_EXISTS))
				RESOURCE_PATH = g_path_get_dirname(test);
		}
	}

	if (!RESOURCE_PATH)
		RESOURCE_PATH = "/usr/local/dwlb";

	StatusNotifierHost *snhost = start_statusnotifierhost();

	const char *bgcolor;
	int i = 1;
	for (; i < argc; i++) {
		char **strings = g_strsplit(argv[i], "=", 0);
		if (strcmp(strings[0], "--height") == 0) {
			snhost->height = atoi(strings[1]);
		} else if (strcmp(strings[0], "--traymon") == 0) {
			snhost->traymon = g_strdup(strings[1]);
		} else if (strcmp(strings[0], "--bg-color") == 0) {
			bgcolor = strdup(strings[1]);
		}
		g_strfreev(strings);
	}

	snhost->cssdata = g_strdup_printf("window{background-color:%s;}", bgcolor);

	GtkApplication *app = gtk_application_new("com.vetu104.Gtktray",
	                                          G_APPLICATION_DEFAULT_FLAGS);

	g_signal_connect(app, "activate", G_CALLBACK(activate), snhost);

	/*
	 * g_unix_signal_add(SIGINT, (GSourceFunc)terminate_app, snhost);
	 * g_unix_signal_add(SIGTERM, (GSourceFunc)terminate_app, snhost);
	 */

	char *argv_inner[] = { progname, NULL };
	int status = g_application_run(G_APPLICATION(app), 1, argv_inner);

	g_object_unref(app);
	return status;
}
