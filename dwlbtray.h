#ifndef GTKTRAY_H
#define GTKTRAY_H

#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

extern const char *RESOURCE_PATH;

typedef struct {
	uint32_t id;
	GDBusProxy* proxy;
} ActionCallbackData;


typedef struct StatusNotifierHost {
	GDBusConnection *conn;
	GDBusNodeInfo *nodeinfo;
	GDBusProxy *watcherproxy;
	GSList *trayitems;
	GtkWidget *box;
	GtkWindow *window;
	int bus_obj_reg_id;
	int cursize;
	int height;
	int margin;
	int noitems;
	int owner_id;
	uint nameowner_sig_sub_id;
	uint watcher_id;
	char *traymon;
} StatusNotifierHost;

typedef struct StatusNotifierItem {
	GDBusNodeInfo *menunodeinfo;
	GDBusNodeInfo *nodeinfo;
	GDBusProxy *menuproxy;
	GDBusProxy *proxy;
	GMenu *menu;
	GSList *action_cb_data_slist;
	GSimpleActionGroup *actiongroup;
	GVariant *iconpixmap_v;
	GdkPaintable *paintable;
	GtkWidget *icon;
	GtkWidget *popovermenu;
	StatusNotifierHost *host;
	char *busname;
	char *busobj;
	char *iconname;
	char *menuobj;
	uint32_t menurevision;
	unsigned char *icon_data;
} StatusNotifierItem;


void create_trayitem(GDBusConnection *conn, GAsyncResult *res, StatusNotifierItem *snitem);
void create_menu(GDBusProxy *proxy, GAsyncResult *res, StatusNotifierItem *snitem);
StatusNotifierHost* start_statusnotifierhost();
// void terminate_statusnotifierhost(StatusNotifierHost *snhost);
GDBusNodeInfo* get_interface_info(const char *xmlpath);
void dwlb_request_resize(StatusNotifierHost *snhost);

#endif /* STATUSNOTIFIERHOST_H */
