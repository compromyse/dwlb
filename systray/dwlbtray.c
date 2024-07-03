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
	char cssdata[64];
	int position;
} args_parsed;

static const int margin = 4;
static const int spacing = 4;

enum {
	DWLB_POSITION_TOP,
	DWLB_POSITION_BOTTOM
};

static void
activate(GtkApplication* app, void *data)
{
	args_parsed *args = (args_parsed*)data;

	GdkDisplay *display = gdk_display_get_default();

	int iconsize, win_default_width, win_default_height;

	iconsize = args->barheight - 2 * margin;
	win_default_width = args->barheight;
	win_default_height = args->barheight;

	GtkCssProvider *css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(css, args->cssdata);
	gtk_style_context_add_provider_for_display(display,
	                                           GTK_STYLE_PROVIDER(css),
	                                           GTK_STYLE_PROVIDER_PRIORITY_USER);
	g_object_unref(css);

	gboolean anchors[4];

	switch (args->position) {
		case DWLB_POSITION_TOP:
			anchors[0] = FALSE;    // left
			anchors[1] = TRUE;     // right
			anchors[2] = TRUE;     // top
			anchors[3] = FALSE;    // bottom
			break;
		case DWLB_POSITION_BOTTOM:
			anchors[0] = FALSE;    // left
			anchors[1] = TRUE;     // right
			anchors[2] = FALSE;    // top
			anchors[3] = TRUE;     // bottom
			break;
		default:
			g_assert_not_reached();
			break;
	}

	GListModel *mons = gdk_display_get_monitors(display);

	// Create tray for each monitor
	for (uint i = 0; i < g_list_model_get_n_items(mons); i++) {
		GdkMonitor *mon = g_list_model_get_item(mons, i);
		const char *conn = gdk_monitor_get_connector(mon);

		SnHost *host = sn_host_new(win_default_width,
		                           win_default_height,
		                           iconsize,
		                           margin,
		                           spacing,
		                           conn);

		GtkWindow *window = GTK_WINDOW(host);

		gtk_window_set_application(window, app);

		gtk_layer_init_for_window(window);
		gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_BOTTOM);
		gtk_layer_set_exclusive_zone(window, -1);

		gtk_layer_set_monitor(window, mon);

		for (int j = 0; j < GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER; j++) {
			gtk_layer_set_anchor(window, j, anchors[j]);
		}

		gtk_window_present(window);
	}
}

static void
terminate_app_helper(void *data, void *udata)
{
	GtkWindow *window = GTK_WINDOW(data);

	gtk_window_close(window);
}

static gboolean
terminate_app(GtkApplication *app)
{
	GList *windows = gtk_application_get_windows(app);
	g_list_foreach(windows, terminate_app_helper, NULL);

    return G_SOURCE_REMOVE;
}

int
main(int argc, char *argv[])
{
	args_parsed args;
	args.barheight = 22;
	args.position = DWLB_POSITION_TOP;

	char *bgcolor = NULL;

	int i = 1;
	for (; i < argc; i++) {
		char **strings = g_strsplit(argv[i], "=", 0);
		if (strcmp(strings[0], "--height") == 0) {
			args.barheight = strtol(strings[1], NULL, 10);
		} else if (strcmp(strings[0], "--bg-color") == 0) {
			bgcolor = strdup(strings[1]);
		} else if (strcmp(strings[0], "--position") == 0) {
			if (strcmp(strings[1], "bottom") == 0)
				args.position = DWLB_POSITION_BOTTOM;
		}
		g_strfreev(strings);
	}

	if (bgcolor) {
		snprintf(args.cssdata, sizeof(args.cssdata), "window{background-color:%s;}", bgcolor);
		g_free(bgcolor);
	} else {
		snprintf(args.cssdata, sizeof(args.cssdata), "window{background-color:%s;}", "#222222");
	}

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
