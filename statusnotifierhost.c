#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "dwlbtray.h"

static void unregister_all(StatusNotifierHost *snhost);
static void sub_finalize(StatusNotifierHost *snhost);
static void busobj_finalize(StatusNotifierHost *snhost);
static void unregister_statusnotifieritem(StatusNotifierItem *snitem);
static void handle_method_call(GDBusConnection* conn, const char* sender,
		const char* object_path, const char* iface, const char* method,
		GVariant* parameters, GDBusMethodInvocation* invocation,
		StatusNotifierHost* snhost
);
static GVariant* handle_get_prop(GDBusConnection* conn, const char* sender,
		const char* obj_path, const char* iface_name,
		const char* prop, GError** error, StatusNotifierHost* snhost
);


static GDBusInterfaceVTable interface_vtable = {
	(GDBusInterfaceMethodCallFunc)handle_method_call,
	(GDBusInterfaceGetPropertyFunc)handle_get_prop,
	NULL
};


static void
add_trayitem_name_to_builder(StatusNotifierItem *snitem, GVariantBuilder *builder)
{
	g_variant_builder_add_value(builder, g_variant_new_string(snitem->busname));
}


static int
find_snitem(StatusNotifierItem *snitem, const char *busname_match)
{
	if (strcmp(snitem->busname, busname_match) == 0)
		return 0;
	else
		return -1;
}


static void
unregister_all_wrap(StatusNotifierItem *snitem, void *data)
{
	unregister_statusnotifieritem(snitem);
}


static void
unregister_all(StatusNotifierHost *snhost)
{
    g_slist_foreach(snhost->trayitems, (GFunc)unregister_all_wrap, NULL);
}


void
dwlb_request_resize(StatusNotifierHost *snhost)
{
	if (snhost->in_exit)
		snhost->curwidth = 0;
	else if (snhost->noitems <= 1)
		snhost->curwidth = 22;
	else
		snhost->curwidth = 22 * snhost->noitems - 6; // dunno why substract 6 to make it align, just trial and error until it worked

	struct sockaddr_un sockaddr;
	sockaddr.sun_family = AF_UNIX;
	char *socketpath = g_strdup_printf("%s/dwlb/dwlb-0", g_get_user_runtime_dir());
	snprintf(sockaddr.sun_path, sizeof(sockaddr.sun_path), "%s", socketpath);
	char *sockbuf = NULL;
	if (snhost->traymon) {
		sockbuf = g_strdup_printf("%s %s %i", snhost->traymon, "resize", snhost->curwidth);
	}
	else {
		sockbuf = g_strdup_printf("%s %s %i", "all", "resize", snhost->curwidth);
	}

	size_t len = strlen(sockbuf);
	int sock_fd = socket(AF_UNIX, SOCK_STREAM, 1);

	int connstatus = connect(sock_fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
	if (connstatus != 0) {
		g_error("Error connecting to dwlb socket");
	}

	if (send(sock_fd, sockbuf, len, 0) == -1)
		g_error("Could not send size update to %s\n", sockaddr.sun_path);
	close(sock_fd);

	g_free(socketpath);
	g_free(sockbuf);
}


static void
register_statusnotifieritem(GDBusConnection *conn,
                            const char *busname,
                            const char *busobj,
                            StatusNotifierHost *snhost)
{
	g_debug("Registering %s\n", busname);
	StatusNotifierItem *snitem;
	snitem = g_malloc0(sizeof(StatusNotifierItem));
	snitem->host = snhost;
	snitem->busname = g_strdup(busname);
	snitem->isclosing = FALSE;
	snitem->host->noitems = snitem->host->noitems + 1;

	snhost->trayitems = g_slist_prepend(snhost->trayitems, snitem);
	dwlb_request_resize(snitem->host);

	GDBusNodeInfo *nodeinfo = g_dbus_node_info_new_for_xml(STATUSNOTIFIERITEM_XML, NULL);

	g_dbus_proxy_new(conn,
                         G_DBUS_PROXY_FLAGS_NONE,
                         nodeinfo->interfaces[0],
                         snitem->busname,
                         busobj,
                         "org.kde.StatusNotifierItem",
			 NULL,
                         (GAsyncReadyCallback)create_trayitem,
                         snitem);

	g_dbus_node_info_unref(nodeinfo);

	GError *err = NULL;
	g_dbus_connection_emit_signal(conn,
	                              NULL,
	                              "/StatusNotifierWatcher",
	                              "org.kde.StatusNotifierWatcher",
	                              "StatusNotifierItemRegistered",
	                              g_variant_new("(s)", snitem->busname),
	                              &err);
	if (err) {
		g_warning("%s\n", err->message);
		g_error_free(err);
	}
}


static void
unregister_statusnotifieritem(StatusNotifierItem *snitem)
{
	g_debug("Unregistering %s\n", snitem->busname);
	if (snitem->popovermenu) {
		gtk_popover_menu_set_menu_model(GTK_POPOVER_MENU(snitem->popovermenu), NULL);
		gtk_widget_unparent(snitem->popovermenu);
		snitem->popovermenu = NULL;
	}

	if (snitem->icon) {
		gtk_widget_insert_action_group(snitem->icon, "menuitem", NULL);
		g_object_unref(snitem->actiongroup);
		gtk_box_remove(GTK_BOX(snitem->host->box), snitem->icon);
		snitem->icon = NULL;
	}

	g_dbus_connection_emit_signal(snitem->host->conn,
                                      NULL,
	                              "/StatusNotifierWatcher",
	                              "org.kde.StatusNotifierWatcher",
	                              "StatusNotifierItemUnregistered",
	                              g_variant_new("(s)", snitem->busname),
	                              NULL);

	if (snitem->menuproxy)
		g_object_unref(snitem->menuproxy);
	if (snitem->proxy)
		g_object_unref(snitem->proxy);

	if (snitem->paintable) {
		g_object_unref(snitem->paintable);
		if (snitem->iconpixmap_v)
			g_variant_unref(snitem->iconpixmap_v);
		if (snitem->iconname)
			g_free(snitem->iconname);
	}

	g_free(snitem->busname);

	snitem->host->trayitems = g_slist_remove(snitem->host->trayitems, snitem);
	snitem->host->noitems = snitem->host->noitems - 1;
	dwlb_request_resize(snitem->host);
	g_free(snitem);
	snitem = NULL;

}


static void
handle_method_call(GDBusConnection *conn,
                   const char *sender,
                   const char *object_path,
                   const char *interface_name,
                   const char *method_name,
                   GVariant *parameters,
                   GDBusMethodInvocation *invocation,
                   StatusNotifierHost *snhost)
{
	if (strcmp(method_name, "RegisterStatusNotifierItem") == 0) {
		const char *param;
		const char *busobj;

		g_variant_get(parameters, "(&s)", &param);

		if (g_str_has_prefix(param, "/")) {
			busobj = param;
		} else {
			busobj = "/StatusNotifierItem";
		}

		register_statusnotifieritem(conn, sender, busobj, snhost);
		g_dbus_method_invocation_return_value(invocation, NULL);

	} else {
		g_dbus_method_invocation_return_dbus_error(invocation,
                                                           "org.freedesktop.DBus.Error.UnknownMethod",
		                                           "Unknown method");
	}
}


static GVariant*
handle_get_prop(GDBusConnection* conn,
                const char* sender,
                const char* object_path,
                const char* interface_name,
                const char* property_name,
                GError** err,
                StatusNotifierHost *snhost)
{
	if (strcmp(property_name, "ProtocolVersion") == 0) {
		return g_variant_new("i", 0);
	} else if (strcmp(property_name, "IsStatusNotifierHostRegistered") == 0) {
		return g_variant_new("b", TRUE);
	} else if (strcmp(property_name, "RegisteredStatusNotifierItems") == 0) {
		if (!snhost->trayitems)
			return g_variant_new("as", NULL);

		GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
		g_slist_foreach(snhost->trayitems, (GFunc)add_trayitem_name_to_builder, builder);
		GVariant *as = g_variant_builder_end(builder);

		g_variant_builder_unref(builder);

		return as;
	} else {
		g_set_error(err,
                            G_DBUS_ERROR,
		            G_DBUS_ERROR_UNKNOWN_PROPERTY,
		            "Unknown property '%s'.",
		            property_name);
		g_error_free(*err);
		return NULL;
	}
}

// ugly
static gboolean
unregister_after_timeout(StatusNotifierItem *snitem)
{
	unregister_statusnotifieritem(snitem);
	return G_SOURCE_REMOVE;
}


// Finds trayitems which dropped from the bus and untracks them
static void
monitor_bus(GDBusConnection* conn,
            const char* sender,
            const char* objpath,
            const char* iface_name,
            const char* signame,
            GVariant *params,
            StatusNotifierHost *snhost)
{
	if (strcmp(signame, "NameOwnerChanged") == 0) {
		if (!snhost->trayitems)
			return;

		const char *name;
		const char *old_owner;
		const char *new_owner;

		g_variant_get(params, "(&s&s&s)", &name, &old_owner, &new_owner);

		if (strcmp(new_owner, "") == 0) {
			GSList *pmatch = g_slist_find_custom(snhost->trayitems, name, (GCompareFunc)find_snitem);
			if (pmatch) {
				StatusNotifierItem *snitem = pmatch->data;
				snitem->isclosing = TRUE;
				// ugly
				g_timeout_add_seconds(2, (GSourceFunc)unregister_after_timeout, snitem);
			}
		}
	}
}


static void
bus_acquired_handler(GDBusConnection *conn, const char *busname, StatusNotifierHost *snhost)
{
	GError *err = NULL;
	GDBusNodeInfo *nodeinfo = g_dbus_node_info_new_for_xml(STATUSNOTIFIERWATCHER_XML, NULL);

	snhost->obj_id = g_dbus_connection_register_object(conn,
                                                           "/StatusNotifierWatcher",
                                                           nodeinfo->interfaces[0],
                                                           &interface_vtable,
                                                           snhost,  // udata
                                                           (GDestroyNotify)busobj_finalize,  // udata_free_func
                                                           &err);

	g_dbus_node_info_unref(nodeinfo);

	if (err) {
		g_error("%s\n", err->message);
		g_error_free(err);
		exit(-1);
	}

	snhost->sub_id = g_dbus_connection_signal_subscribe(conn,
                                                            NULL,  // Listen to all senders);
                                                            "org.freedesktop.DBus",
	                                                    "NameOwnerChanged",
	                                                    NULL,  // Match all obj paths
	                                                    NULL,  // Match all arg0s
	                                                    G_DBUS_SIGNAL_FLAGS_NONE,
	                                                    (GDBusSignalCallback)monitor_bus,
	                                                    snhost,
	                                                    (GDestroyNotify)sub_finalize);
}


static void
name_acquired_handler(GDBusConnection *conn, const char *busname, StatusNotifierHost *snhost)
{
	GError *err = NULL;
	snhost->conn = conn;

	g_dbus_connection_emit_signal(conn,
                                      NULL,
                                      "/StatusNotifierWatcher",
                                      "org.kde.StatusNotifierWatcher",
                                      "StatusNotifierHostRegistered",
                                      NULL,
                                      &err);

	if (err) {
		g_warning("%s\n", err->message);
		g_error_free(err);
	}
}


static void
name_lost_handler(GDBusConnection *conn, const char *busname, StatusNotifierHost *snhost)
{
	g_error("Could not acquire %s\n", busname);
	exit(-1);
}


static void
snhost_finalize(StatusNotifierHost *snhost)
{
	snhost->in_exit = TRUE;
	dwlb_request_resize(snhost);
	gtk_window_close(snhost->window);
	g_free(snhost);
	snhost = NULL;
}


static void
busobj_finalize(StatusNotifierHost *snhost)
{
	unregister_all(snhost);
	g_slist_free(snhost->trayitems);
	g_bus_unown_name(snhost->owner_id);
}


static void
sub_finalize(StatusNotifierHost *snhost)
{
	g_dbus_connection_unregister_object(snhost->conn, snhost->obj_id);
}


void
terminate_statusnotifierhost(StatusNotifierHost *snhost)
{
	g_dbus_connection_signal_unsubscribe(snhost->conn, snhost->sub_id);
}


StatusNotifierHost*
start_statusnotifierhost()
{
	StatusNotifierHost *snhost = g_malloc0(sizeof(StatusNotifierHost));

	snhost->height = 22;
	snhost->margin = 4;
	snhost->noitems = 0;

	snhost->owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
                                          "org.kde.StatusNotifierWatcher",
                                          G_BUS_NAME_OWNER_FLAGS_NONE,
                                          (GBusAcquiredCallback)bus_acquired_handler,
                                          (GBusNameAcquiredCallback)name_acquired_handler,
                                          (GBusNameLostCallback)name_lost_handler,
                                          snhost,
                                          (GDestroyNotify)snhost_finalize);

	return snhost;
}
