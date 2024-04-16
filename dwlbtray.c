#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>

#include "dwlbtray.h"


static void
activate(GtkApplication* app, StatusNotifierHost *snhost)
{
	GdkDisplay *display = gdk_display_get_default();

	GtkWindow *window = GTK_WINDOW(gtk_application_window_new(app));
	gtk_window_set_default_size(GTK_WINDOW(window), 22, snhost->height);

	GtkCssProvider *css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(css, snhost->cssdata);
	gtk_style_context_add_provider_for_display(display,
	                                           GTK_STYLE_PROVIDER(css),
	                                           GTK_STYLE_PROVIDER_PRIORITY_USER);
	gtk_widget_add_css_class(GTK_WIDGET(window), "dwlbtray");
	g_free(snhost->cssdata);
	snhost->cssdata = NULL;


	gtk_layer_init_for_window(window);
	gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_BOTTOM);
	gtk_layer_set_exclusive_zone(window, -1);
	static gboolean anchors[] = {FALSE, TRUE, TRUE, FALSE};
	if (snhost->position == 1) {
		anchors[0] = FALSE;   // left
		anchors[1] = TRUE;    // right
		anchors[2] = FALSE;   // top
		anchors[3] = TRUE;    // bottom
	}
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

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_widget_set_vexpand(box, TRUE);
	gtk_widget_set_hexpand(box, TRUE);
	gtk_widget_set_margin_start(box, snhost->margin);
	gtk_widget_set_margin_end(box, snhost->margin);
	gtk_window_set_child(window, box);

	dwlb_request_resize(snhost);
	gtk_window_present(window);

	snhost->box = box;
	snhost->window = window;
}


static gboolean
terminate_app(StatusNotifierHost *snhost)
{
    terminate_statusnotifierhost(snhost);

    return G_SOURCE_REMOVE;
}


int
main(int argc, char *argv[])
{
	StatusNotifierHost *snhost = start_statusnotifierhost();

	char *bgcolor = NULL;
	char *cssdata;
	int position = 0;

	int i = 1;
	for (; i < argc; i++) {
		char **strings = g_strsplit(argv[i], "=", 0);
		if (strcmp(strings[0], "--height") == 0) {
			snhost->height = atoi(strings[1]);
		} else if (strcmp(strings[0], "--traymon") == 0) {
			snhost->traymon = g_strdup(strings[1]);
		} else if (strcmp(strings[0], "--bg-color") == 0) {
			bgcolor = strdup(strings[1]);
		} else if (strcmp(strings[0], "--position") == 0) {
			if (strcmp(strings[1], "bottom") == 0)
				position = 1;
		}
		g_strfreev(strings);
	}

	snhost->position = position;

	if (bgcolor)
		cssdata = g_strdup_printf("window.dwlbtray{background-color:%s;}", bgcolor);
	else
		cssdata = g_strdup_printf("window.dwlbtray{background-color:%s;}", "#222222");

	snhost->cssdata = cssdata;
	g_free(bgcolor);

	GtkApplication *app = gtk_application_new("org.dwlb.dwlbtray",
	                                          G_APPLICATION_DEFAULT_FLAGS);

	g_signal_connect(app, "activate", G_CALLBACK(activate), snhost);

	g_unix_signal_add(SIGINT, (GSourceFunc)terminate_app, snhost);
	g_unix_signal_add(SIGTERM, (GSourceFunc)terminate_app, snhost);

	char *argv_inner[] = { argv[0], NULL };
	int status = g_application_run(G_APPLICATION(app), 1, argv_inner);

	g_object_unref(app);
	return status;
}
