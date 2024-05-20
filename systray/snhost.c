#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "snhost.h"
#include "snitem.h"


struct _SnHost
{
	GtkWidget parent_instance;

	char *traymon;
	int iconsize;
	int margins;
	int spacing;

	GDBusConnection *conn;
	int owner_id;
	int obj_reg_id;
	int sig_sub_id;

	int nitems;
	int curwidth;
	GSList *trayitems;
	gboolean exiting;
};

G_DEFINE_FINAL_TYPE(SnHost, sn_host, GTK_TYPE_BOX)

enum
{
	PROP_TRAYMON = 1,
	PROP_ICONSIZE,
	PROP_MARGINS,
	PROP_SPACING,
	N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };


static void		sn_host_constructed			(GObject *obj);
static void		sn_host_dispose				(GObject *obj);
static void		sn_host_finalize			(GObject *obj);

static void		sn_host_bus_call_method_handler		(GDBusConnection *conn,
								const char *sender,
								const char *object_path,
								const char *interface_name,
								const char *method_name,
								GVariant *parameters,
								GDBusMethodInvocation *invocation,
								void *data);

static GVariant*	sn_host_bus_prop_get_handler		(GDBusConnection* conn,
								const char* sender,
								const char* object_path,
								const char* interface_name,
								const char* property_name,
								GError** err,
								void *data);

static GDBusInterfaceVTable interface_vtable = {
	sn_host_bus_call_method_handler,
	sn_host_bus_prop_get_handler,
	NULL
};


void
dwlb_request_resize(SnHost *self)
{
	if (self->exiting)
		self->curwidth = 0;
	else if (self->nitems <= 1)
		self->curwidth = self->iconsize + 2 * self->margins;
	else
		self->curwidth = self->nitems * self->iconsize +
		                 (self->nitems - 1) * self->spacing +
		                 2 * self->margins;

	struct sockaddr_un sockaddr;
	sockaddr.sun_family = AF_UNIX;
	char *socketpath = g_strdup_printf("%s/dwlb/dwlb-0", g_get_user_runtime_dir());
	snprintf(sockaddr.sun_path, sizeof(sockaddr.sun_path), "%s", socketpath);
	char *sockbuf = NULL;
	if (self->traymon) {
		sockbuf = g_strdup_printf("%s %s %i", self->traymon, "resize", self->curwidth);
	}
	else {
		sockbuf = g_strdup_printf("%s %s %i", "all", "resize", self->curwidth);
	}

	size_t len = strlen(sockbuf);
	int sock_fd = socket(AF_UNIX, SOCK_STREAM, 1);

	int connstatus = connect(sock_fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
	if (connstatus != 0) {
		g_error("Error connecting to dwlb socket");
	}

	if (send(sock_fd, sockbuf, len, 0) == -1)
		g_error("Could not send size update to %s", sockaddr.sun_path);
	close(sock_fd);

	g_free(socketpath);
	g_free(sockbuf);
}

static void
sn_host_register_item(SnHost *self,
                      const char *busname,
                      const char *busobj)
{
	g_debug("Registering %s", busname);
	SnItem *snitem = sn_item_new(busname, busobj, self->iconsize);
	gtk_box_append(GTK_BOX(self), GTK_WIDGET(snitem));
	self->nitems = self->nitems + 1;
	self->trayitems = g_slist_prepend(self->trayitems, snitem);
	dwlb_request_resize(self);

	GError *err = NULL;
	g_dbus_connection_emit_signal(self->conn,
	                              NULL,
	                              "/StatusNotifierWatcher",
	                              "org.kde.StatusNotifierWatcher",
	                              "StatusNotifierItemRegistered",
	                              g_variant_new("(s)", busname),
	                              &err);
	if (err) {
		g_warning("%s", err->message);
		g_error_free(err);
	}
}

static void
sn_host_unregister_item(SnHost *self, SnItem *snitem)
{
	char *busname = sn_item_get_busname(snitem);
	g_debug("Unregistering %s", busname);

	self->trayitems = g_slist_remove(self->trayitems, snitem);

	g_object_ref(snitem);
	gtk_box_remove(GTK_BOX(self), GTK_WIDGET(snitem));
	g_object_run_dispose(G_OBJECT(snitem));
	g_object_unref(snitem);

	self->nitems = self->nitems - 1;

	dwlb_request_resize(self);

	GError *err = NULL;

	g_dbus_connection_emit_signal(self->conn,
                                      NULL,
	                              "/StatusNotifierWatcher",
	                              "org.kde.StatusNotifierWatcher",
	                              "StatusNotifierItemUnregistered",
	                              g_variant_new("(s)", busname),
	                              &err);
	if (err) {
		g_warning("%s", err->message);
		g_error_free(err);
	}

	g_free(busname);
}

static int
nameowner_find_helper(SnItem *snitem, const char *busname_match)
{
	char *busname = sn_item_get_busname(snitem);
	int ret = 1;
	if (strcmp(busname, busname_match) == 0)
		ret =  0;
	else
		ret = -1;

	g_free(busname);
	return ret;
}

static void
sn_host_bus_monitor(GDBusConnection* conn,
                   const char* sender,
                   const char* objpath,
                   const char* iface_name,
                   const char* signame,
                   GVariant *params,
                   void *data)
{
	SnHost *self = SN_HOST(data);

	if (strcmp(signame, "NameOwnerChanged") == 0) {
		if (!self->trayitems)
			return;

		const char *name;
		const char *old_owner;
		const char *new_owner;
		g_variant_get(params, "(&s&s&s)", &name, &old_owner, &new_owner);
		if (strcmp(new_owner, "") == 0) {
			GSList *pmatch = g_slist_find_custom(self->trayitems, name, (GCompareFunc)nameowner_find_helper);
			if (pmatch) {
				SnItem *snitem = pmatch->data;
				sn_host_unregister_item(self, snitem);
			}
		}

	}
}

static void
unregister_all_helper(SnItem* item, SnHost *host)
{
	sn_host_unregister_item(host, item);
}


static void
sn_host_unregister_all(SnHost *self)
{
    g_slist_foreach(self->trayitems, (GFunc)unregister_all_helper, self);
}

static void
sn_host_bus_call_method_handler(GDBusConnection *conn,
                        const char *sender,
                        const char *obj_path,
                        const char *iface_name,
                        const char *method_name,
                        GVariant *params,
                        GDBusMethodInvocation *invoc,
                        void *data)
{
	SnHost *self = SN_HOST(data);

	if (strcmp(method_name, "RegisterStatusNotifierItem") == 0) {
		const char *param;
		const char *busobj;
		const char *registree_name;

		g_variant_get(params, "(&s)", &param);

		if (g_str_has_prefix(param, "/")) {
			busobj = param;
		} else {
			busobj = "/StatusNotifierItem";
		}

		if (g_str_has_prefix(param, ":") && strcmp(sender, param) != 0)
			registree_name = param;
		else
			registree_name = sender;

		sn_host_register_item(self, registree_name, busobj);
		g_dbus_method_invocation_return_value(invoc, NULL);

	} else {
		g_dbus_method_invocation_return_dbus_error(invoc,
                                                           "org.freedesktop.DBus.Error.UnknownMethod",
		                                           "Unknown method");
	}
}

static void
bus_get_snitems_helper(void *data, void *udata)
{
	SnItem *item = SN_ITEM(data);
	GVariantBuilder *builder = (GVariantBuilder*)udata;

	char *busname = sn_item_get_busname(item);
	g_variant_builder_add_value(builder, g_variant_new_string(busname));
	g_free(busname);
}

static GVariant*
sn_host_bus_prop_get_handler(GDBusConnection* conn,
                     const char* sender,
                     const char* object_path,
                     const char* interface_name,
                     const char* property_name,
                     GError** err,
                     void *data)
{
	SnHost *self = SN_HOST(data);

	if (strcmp(property_name, "ProtocolVersion") == 0) {
		return g_variant_new("i", 0);
	} else if (strcmp(property_name, "IsStatusNotifierHostRegistered") == 0) {
		return g_variant_new("b", TRUE);
	} else if (strcmp(property_name, "RegisteredStatusNotifierItems") == 0) {
		if (!self->trayitems)
			return g_variant_new("as", NULL);

		GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
		g_slist_foreach(self->trayitems, bus_get_snitems_helper, builder);
		GVariant *as = g_variant_builder_end(builder);

		g_variant_builder_unref(builder);

		return as;
	} else {
		g_set_error(err,
                            G_DBUS_ERROR,
		            G_DBUS_ERROR_UNKNOWN_PROPERTY,
		            "Unknown property '%s'.",
		            property_name);
		return NULL;
	}
}

static void
sn_host_bus_acquired_handler(GDBusConnection *conn, const char *busname, void *data)
{
	SnHost *self = SN_HOST(data);

	self->conn = conn;

	GError *err = NULL;
	GDBusNodeInfo *nodeinfo = g_dbus_node_info_new_for_xml(STATUSNOTIFIERWATCHER_XML, NULL);

	self->obj_reg_id =
	        g_dbus_connection_register_object(self->conn,
	                                          "/StatusNotifierWatcher",
	                                          nodeinfo->interfaces[0],
	                                          &interface_vtable,
	                                          self,
	                                          NULL,
	                                          &err);

	g_dbus_node_info_unref(nodeinfo);

	if (err) {
		g_error("%s", err->message);
		g_error_free(err);
		exit(-1);
	}

	self->sig_sub_id =
	        g_dbus_connection_signal_subscribe(self->conn,
	                                           NULL,  // Listen to all senders);
	                                           "org.freedesktop.DBus",
	                                           "NameOwnerChanged",
	                                           NULL,  // Match all obj paths
	                                           NULL,  // Match all arg0s
	                                           G_DBUS_SIGNAL_FLAGS_NONE,
	                                           sn_host_bus_monitor,
	                                           self,
	                                           NULL);
}

static void
sn_host_bus_name_acquired_handler(GDBusConnection *conn, const char *busname, void *data)
{
	SnHost *self = SN_HOST(data);

	GError *err = NULL;

	g_dbus_connection_emit_signal(self->conn,
                                      NULL,
                                      "/StatusNotifierWatcher",
                                      "org.kde.StatusNotifierWatcher",
                                      "StatusNotifierHostRegistered",
                                      NULL,
                                      &err);

	if (err) {
		g_warning("%s", err->message);
		g_error_free(err);
	}
}

static void
sn_host_bus_name_lost_handler(GDBusConnection *conn, const char *busname, void *data)
{
	g_error("Could not acquire %s, maybe another instance is running?", busname);
	exit(-1);
}

static void
sn_host_set_property(GObject *object, uint property_id, const GValue *value, GParamSpec *pspec)
{
	SnHost *self = SN_HOST(object);

	switch (property_id) {
		case PROP_TRAYMON:
			self->traymon = g_strdup(g_value_get_string(value));
			break;
		case PROP_ICONSIZE:
			self->iconsize = g_value_get_int(value);
			break;
		case PROP_MARGINS:
			self->margins = g_value_get_int(value);
			break;
		case PROP_SPACING:
			self->spacing = g_value_get_int(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
	}
}

static void
sn_host_get_property(GObject *object, uint property_id, GValue *value, GParamSpec *pspec)
{
	G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
}

static void
sn_host_class_init(SnHostClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->set_property = sn_host_set_property;
	object_class->get_property = sn_host_get_property;
	object_class->constructed = sn_host_constructed;
	object_class->dispose = sn_host_dispose;
	object_class->finalize = sn_host_finalize;

	obj_properties[PROP_ICONSIZE] =
		g_param_spec_int("iconsize", NULL, NULL,
		                 G_MININT,
		                 G_MAXINT,
		                 22,
				 G_PARAM_CONSTRUCT_ONLY |
		                 G_PARAM_WRITABLE |
		                 G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_MARGINS] =
		g_param_spec_int("margins", NULL, NULL,
		                 G_MININT,
		                 G_MAXINT,
		                 4,
				 G_PARAM_CONSTRUCT_ONLY |
				 G_PARAM_WRITABLE |
		                 G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_SPACING] =
		g_param_spec_int("spacing", NULL, NULL,
		                 G_MININT,
		                 G_MAXINT,
		                 4,
				 G_PARAM_CONSTRUCT_ONLY |
				 G_PARAM_WRITABLE |
		                 G_PARAM_STATIC_STRINGS);
	obj_properties[PROP_TRAYMON] =
		g_param_spec_string("traymon", NULL, NULL,
		                 NULL,
				 G_PARAM_CONSTRUCT_ONLY |
				 G_PARAM_WRITABLE |
		                 G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties(object_class, N_PROPERTIES, obj_properties);
}

static void
sn_host_init(SnHost *self)
{
	self->exiting = FALSE;
	self->nitems = 0;
	self->trayitems = NULL;

	self->owner_id =
	        g_bus_own_name(G_BUS_TYPE_SESSION,
	                       "org.kde.StatusNotifierWatcher",
	                       G_BUS_NAME_OWNER_FLAGS_NONE,
	                       sn_host_bus_acquired_handler,
	                       sn_host_bus_name_acquired_handler,
	                       sn_host_bus_name_lost_handler,
	                       self,
	                       NULL);
}

static void
sn_host_constructed(GObject *obj)
{
	SnHost *self = SN_HOST(obj);
	GtkWidget *widget = GTK_WIDGET(obj);
	gtk_widget_set_vexpand(widget, TRUE);
	gtk_widget_set_hexpand(widget, TRUE);
	gtk_widget_set_margin_start(widget, self->margins);
	gtk_widget_set_margin_end(widget, self->margins);
	gtk_widget_set_margin_top(widget, self->margins);
	gtk_widget_set_margin_bottom(widget, self->margins);
	gtk_box_set_homogeneous(GTK_BOX(obj), TRUE);
	gtk_box_set_spacing(GTK_BOX(obj), self->margins);

	G_OBJECT_CLASS(sn_host_parent_class)->constructed(obj);
}

static void
sn_host_dispose(GObject *obj)
{
	g_debug("Disposing snhost");
	SnHost *self = SN_HOST(obj);
	self->exiting = TRUE;

	sn_host_unregister_all(self);

	if (self->sig_sub_id > 0) {
		g_dbus_connection_signal_unsubscribe(self->conn, self->sig_sub_id);
		self->sig_sub_id = 0;
	}

	if (self->obj_reg_id > 0) {
		g_dbus_connection_unregister_object(self->conn, self->obj_reg_id);
		self->obj_reg_id = 0;
	}

	if (self->owner_id > 0) {
		g_bus_unown_name(self->owner_id);
		self->owner_id = 0;
		self->conn = NULL;
	}

	dwlb_request_resize(self);

	G_OBJECT_CLASS(sn_host_parent_class)->dispose(obj);
}

static void
sn_host_finalize(GObject *obj)
{
	SnHost *self = SN_HOST(obj);

	g_free(self->traymon);
	g_slist_free(self->trayitems);

	G_OBJECT_CLASS(sn_host_parent_class)->finalize(obj);
}

SnHost*
sn_host_new(const char *traymon, int iconsize, int margins, int spacing)
{
	return g_object_new(SN_TYPE_HOST,
	                    "traymon", traymon,
	                    "iconsize", iconsize,
	                    "margins", margins,
	                    "spacing", spacing,
	                    NULL);
}
