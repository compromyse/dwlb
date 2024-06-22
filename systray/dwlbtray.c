#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>

#include "snhost.h"

typedef struct args_parsed {
	int barheight;
	char traymon[1024];
	char cssdata[1024];
	int position;
} args_parsed;

static int margin = 4;
static int spacing = 4;

static void
activate(GtkApplication* app, void *data)
{
	args_parsed *args = (args_parsed*)data;

	GdkDisplay *display = gdk_display_get_default();

	int iconsize, win_default_width, win_default_height;

	iconsize = args->barheight - 2 * margin;
	win_default_width = args->barheight;
	win_default_height = args->barheight;

	GtkWindow *window = GTK_WINDOW(gtk_window_new());
	gtk_window_set_decorated(window, FALSE);
	gtk_window_set_default_size(window, win_default_width, win_default_height);
	gtk_window_set_application(window, app);

	GtkCssProvider *css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(css, args->cssdata);
	gtk_style_context_add_provider_for_display(display,
	                                           GTK_STYLE_PROVIDER(css),
	                                           GTK_STYLE_PROVIDER_PRIORITY_USER);
	gtk_widget_add_css_class(GTK_WIDGET(window), "dwlbtray");
	g_object_unref(css);

	gtk_layer_init_for_window(window);
	gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_BOTTOM);
	gtk_layer_set_exclusive_zone(window, -1);
	static gboolean anchors[] = {FALSE, TRUE, TRUE, FALSE};
	if (args->position == 1) {
		anchors[0] = FALSE;   // left
		anchors[1] = TRUE;    // right
		anchors[2] = FALSE;   // top
		anchors[3] = TRUE;    // bottom
	}
	for (int i = 0; i < GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER; i++) {
		gtk_layer_set_anchor(window, i, anchors[i]);
	}

	const char *traymon = NULL;

	if (strcmp(args->traymon, "") != 0) {
		traymon = args->traymon;
	}

	GListModel *mons = gdk_display_get_monitors(display);
	if (traymon) {
		for (uint i = 0; i < g_list_model_get_n_items(mons); i++) {
			GdkMonitor *mon = g_list_model_get_item(mons, i);
			const char *conn = gdk_monitor_get_connector(mon);
			if (strcmp(conn, traymon) == 0) {
				gtk_layer_set_monitor(window, mon);
			}
		}
	} else {
		GdkMonitor *mon = g_list_model_get_item(mons, 0);
		const char *conn = gdk_monitor_get_connector(mon);
		traymon = conn;
		gtk_layer_set_monitor(window, mon);
	}


	SnHost *host = sn_host_new(traymon, iconsize, margin, spacing);
	GtkWidget *widget = GTK_WIDGET(host);
	gtk_window_set_child(window, widget);

	gtk_window_present(window);
}


static gboolean
terminate_app(GtkApplication *app)
{
	GtkWindow *win = gtk_application_get_active_window(app);
	gtk_window_close(win);

    return G_SOURCE_REMOVE;
}

int
main(int argc, char *argv[])
{
	args_parsed args;
	args.barheight = 22;
	args.position = 0;
	*args.traymon = '\0';
	*args.cssdata = '\0';

	char *bgcolor = NULL;
	int position = 0;

	int i = 1;
	for (; i < argc; i++) {
		char **strings = g_strsplit(argv[i], "=", 0);
		if (strcmp(strings[0], "--height") == 0) {
			args.barheight = atoi(strings[1]);
		} else if (strcmp(strings[0], "--traymon") == 0) {
			strncpy(args.traymon, strings[1], sizeof(args.traymon));
		} else if (strcmp(strings[0], "--bg-color") == 0) {
			bgcolor = strdup(strings[1]);
		} else if (strcmp(strings[0], "--position") == 0) {
			if (strcmp(strings[1], "bottom") == 0)
				position = 1;
		}
		g_strfreev(strings);
	}

	args.position = position;

	if (bgcolor)
		snprintf(args.cssdata, sizeof(args.cssdata), "window.dwlbtray{background-color:%s;}", bgcolor);
	else
		snprintf(args.cssdata, sizeof(args.cssdata), "window.dwlbtray{background-color:%s;}", "#222222");

	GtkApplication *app = gtk_application_new("org.dwlb.dwlbtray",
	                                          G_APPLICATION_DEFAULT_FLAGS);

	g_signal_connect(app, "activate", G_CALLBACK(activate), &args);

	g_unix_signal_add(SIGINT, (GSourceFunc)terminate_app, app);
	g_unix_signal_add(SIGTERM, (GSourceFunc)terminate_app, app);

	char *argv_inner[] = { argv[0], NULL };
	int status = g_application_run(G_APPLICATION(app), 1, argv_inner);

	g_object_unref(app);

	return status;
}
