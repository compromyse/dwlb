#include <stdint.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "snitem.h"
#include "sndbusmenu.h"


struct _SnDbusmenu {
	GObject parent_instance;

	char *busname;
	char *busobj;
	SnItem *snitem;

	GDBusProxy *proxy;

	uint32_t revision;
};

G_DEFINE_FINAL_TYPE(SnDbusmenu, sn_dbusmenu, G_TYPE_OBJECT)

enum
{
	PROP_BUSNAME=1,
	PROP_BUSOBJ,
	PROP_SNITEM,
	PROP_PROXY,
	N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };


typedef struct {
	uint32_t id;
	GDBusProxy* proxy;
} ActionCallbackData;

static void		sn_dbusmenu_constructed		(GObject *object);
static void		sn_dbusmenu_dispose		(GObject *object);
static void		sn_dbusmenu_finalize		(GObject *object);

static void		sn_dbusmenu_get_property	(GObject *object,
							uint property_id,
							GValue *value,
							GParamSpec *pspec);

static void		sn_dbusmenu_set_property	(GObject *object,
							uint property_id,
							const GValue *value,
							GParamSpec *pspec);

static GMenu*		create_menumodel		(GVariant *data, SnDbusmenu *self);


static gboolean
check_menuitem_visible(GVariant *data)
{
	gboolean isvisible = TRUE;
	GVariant *menu_data = g_variant_get_child_value(data, 1);
	g_variant_lookup(menu_data, "visible", "b", &isvisible);

	g_variant_unref(menu_data);

	return isvisible;
}

static gboolean
check_has_sections(GVariant *data)
{
	gboolean ret = FALSE;
	char *val;
	GVariant *menu_data;

	GVariantIter iter;
	g_variant_iter_init(&iter, data);
	while ((g_variant_iter_next(&iter, "v", &menu_data))) {
		GVariant *menuitem_data = g_variant_get_child_value(menu_data, 1);
		gboolean check = g_variant_lookup(menuitem_data, "type", "&s", &val);
		if (check && strcmp(val, "separator") == 0)
			ret = TRUE;
		g_variant_unref(menuitem_data);
		g_variant_unref(menu_data);
	}

	return ret;
}

static void
action_activated_cb(GSimpleAction *action, GVariant* param, ActionCallbackData *data)
{
	g_dbus_proxy_call(data->proxy,
	                  "Event",
	                  g_variant_new("(isvu)",
	                                data->id,
	                                "clicked",
	                                g_variant_new_string(""),
	                                time(NULL)),
	                  G_DBUS_CALL_FLAGS_NONE,
	                  -1,
	                  NULL,
	                  NULL,
	                  NULL);
}

static void
action_free(void *data, GClosure *closure)
{
	ActionCallbackData *acbd = (ActionCallbackData *)data;
	g_free(acbd);
}

static GSimpleAction*
create_action(uint32_t id, SnDbusmenu *self)
{
	char *action_name = g_strdup_printf("%u", id);
	GSimpleAction *action = g_simple_action_new(action_name, NULL);

	ActionCallbackData *data = g_malloc(sizeof(ActionCallbackData));
	data->id = id;
	data->proxy = self->proxy;

	g_signal_connect_data(action,
	                      "activate",
	                      G_CALLBACK(action_activated_cb),
	                      data,
	                      action_free,
	                      G_CONNECT_DEFAULT);

	g_free(action_name);

	return action;
}

static GMenuItem*
create_menuitem(GVariant *data, SnDbusmenu *self)
{
	//  (ia{sv}av)
	// GVariant *data
	GMenuItem *menuitem = NULL;

	int32_t id;
	// a{sv]
	GVariant *menu_data;

	g_variant_get_child(data, 0, "i", &id);
	menu_data = g_variant_get_child_value(data, 1);

	const char *label = NULL;
	const char *type = NULL;
	gboolean isenabled = TRUE;
	gboolean isvisible = TRUE;
	gboolean has_submenu = FALSE;

	/* 
	 * gboolean ischeckmark = FALSE;
	 * gboolean isradio = FALSE;
	 * int32_t toggle_state = 99;
	 * const char *toggle_type = NULL;
	 */

	const char *has_submenu_s = NULL;
	GVariantDict dict;
	g_variant_dict_init(&dict, menu_data);
	g_variant_dict_lookup(&dict, "label", "&s", &label);
	g_variant_dict_lookup(&dict, "type", "&s", &type);
	g_variant_dict_lookup(&dict, "enabled", "b", &isenabled);
	g_variant_dict_lookup(&dict, "visible", "b", &isvisible);
	g_variant_dict_lookup(&dict, "children-display", "&s", &has_submenu_s);

	/* 
	 * g_variant_dict_lookup(&dict, "toggle-type", "&s", &toggle_type);
	 * g_variant_dict_lookup(&dict, "toggle-state", "i", &toggle_state);
	 */

	g_variant_dict_clear(&dict);

	if (has_submenu_s && strcmp(has_submenu_s, "submenu") == 0)
		has_submenu = TRUE;

	/* 
	 * if (toggle_type && strcmp(toggle_type, "checkmark") == 0)
	 * 	ischeckmark = TRUE;
	 * else if (toggle_type && strcmp(toggle_type, "radio") == 0)
	 * 	isradio = TRUE;
	 */

	if ((label && isvisible && isenabled) && !(type && strcmp(type, "separator") == 0)) {
		GSimpleAction *action = create_action(id, self);
		char *action_name = g_strdup_printf("%s.%u", "menuitem", id);
		sn_item_add_action(self->snitem, action);
		menuitem = g_menu_item_new(label, action_name);

		g_free(action_name);
		g_object_unref(action);

	} else if ((label && !(type && strcmp(type, "separator") == 0))) {
		GSimpleAction *action = create_action(id, self);
		g_simple_action_set_enabled(action, FALSE);
		char *action_name = g_strdup_printf("%s.%u", "menuitem", id);
		sn_item_add_action(self->snitem, action);
		menuitem = g_menu_item_new(label, action_name);

		g_free(action_name);
		g_object_unref(action);
	}

	if (has_submenu) {
		GVariant *submenu_data = g_variant_get_child_value(data, 2);
		GMenu *submenu = create_menumodel(submenu_data, self);
		g_menu_item_set_submenu(menuitem, G_MENU_MODEL(submenu));
		g_object_unref(submenu);
		g_variant_unref(submenu_data);
	}

	g_variant_unref(menu_data);

	return menuitem;
}

static GMenu*
create_menumodel(GVariant *data, SnDbusmenu *self)
{
	GMenu *ret = g_menu_new();
	GVariantIter iter;
	GVariant *menuitem_data;
	gboolean has_sections = check_has_sections(data);

	if (has_sections) {
		GMenu *section = g_menu_new();
		g_variant_iter_init(&iter, data);
		while ((g_variant_iter_next(&iter, "v", &menuitem_data))) {
			if (!check_menuitem_visible(menuitem_data)) {
				g_variant_unref(menuitem_data);
				continue;
			}

			GMenuItem *menuitem = create_menuitem(menuitem_data, self);
			if (menuitem) {
				g_menu_append_item(section, menuitem);
				g_object_unref(menuitem);
			}
			// menuitem == NULL means menuitem is a separator
			else {
				g_menu_append_section(ret, NULL, G_MENU_MODEL(section));
				g_object_unref(section);
				section = g_menu_new();
			}
			g_variant_unref(menuitem_data);
		}
		g_menu_append_section(ret, NULL, G_MENU_MODEL(section));
		g_object_unref(section);

	} else {
		g_variant_iter_init(&iter, data);
		while ((g_variant_iter_next(&iter, "v", &menuitem_data))) {
			GMenuItem *menuitem = create_menuitem(menuitem_data, self);
			if (menuitem) {
				g_menu_append_item(ret, menuitem);
				g_object_unref(menuitem);
			}
			g_variant_unref(menuitem_data);
		}
	}

	return ret;
}

static void
on_layout_updated(GDBusProxy *proxy, GAsyncResult *res, void *data)
{
	SnDbusmenu *dbusmenu = SN_DBUSMENU(data);

	GError *err = NULL;
	GVariant *retvariant = g_dbus_proxy_call_finish(proxy, res, &err);

	// Errors which might occur when the tray is running slowly (eg under valgrind)
	// and user is spam clicking already exited icons

	// "No such object path '/MenuBar'
	if (err && g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_OBJECT)) {
		g_error_free(err);
		g_object_unref(dbusmenu);
		return;

	// "The name is not activatable"
	} else if (err && g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN)) {
		g_error_free(err);
		g_object_unref(dbusmenu);
		return;

	// "Remote peer disconnected"
	} else if (err && g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_NO_REPLY)) {
		g_error_free(err);
		g_object_unref(dbusmenu);
		return;

	} else if (err) {
		g_warning("%s\n", err->message);
		g_error_free(err);
		g_object_unref(dbusmenu);
		return;
	}

	GVariant *layout;
	GVariant *menuitems;

	layout = g_variant_get_child_value(retvariant, 1);
	menuitems = g_variant_get_child_value(layout, 2);

	GMenu *newmenu = create_menumodel(menuitems, dbusmenu);
	sn_item_set_menu_model(dbusmenu->snitem, newmenu);

	g_variant_unref(menuitems);
	g_variant_unref(layout);
	g_variant_unref(retvariant);
	g_object_unref(dbusmenu);
}


// We just rebuild the entire menu every time...
// ItemsPropertiesUpdated signal would carry data of
// which menuitems got updated and which got removed
// but the GMenu api doesn't allow easy manipulation as it is
static void
on_menuproxy_signal(GDBusProxy *proxy,
                    const char *sender,
                    const char *signal,
                    GVariant *params,
                    void *data)
{
	SnDbusmenu *dbusmenu = SN_DBUSMENU(data);
	if (strcmp(signal, "LayoutUpdated") == 0) {
		uint32_t revision = UINT32_MAX;
		int32_t parentid;
		g_variant_get(params, "(ui)", &revision, &parentid);
		if (dbusmenu->revision != UINT32_MAX && revision <= dbusmenu->revision) {
			// g_debug("%s got %s, but menurevision didn't change. Ignoring\n", snitem->busname, signal);
			return;
		} else {
			dbusmenu->revision = revision;
		}

		g_dbus_proxy_call(dbusmenu->proxy,
		                  "GetLayout",
		                  g_variant_new("(iias)", 0, -1, NULL),
		                  G_DBUS_CALL_FLAGS_NONE,
		                  -1,
		                  NULL,
		                  (GAsyncReadyCallback)on_layout_updated,
		                  g_object_ref(dbusmenu));

	} else if (strcmp(signal, "ItemsPropertiesUpdated") == 0) {
		g_dbus_proxy_call(dbusmenu->proxy,
		                  "GetLayout",
		                  g_variant_new("(iias)", 0, -1, NULL),
		                  G_DBUS_CALL_FLAGS_NONE,
		                  -1,
		                  NULL,
		                  (GAsyncReadyCallback)on_layout_updated,
		                  g_object_ref(dbusmenu));
	}
}

static void
on_menulayout_ready(GDBusProxy *proxy, GAsyncResult *res, void *data)
{
	SnDbusmenu *dbusmenu = SN_DBUSMENU(data);

	GError *err = NULL;
	GVariant *retvariant = g_dbus_proxy_call_finish(proxy, res, &err);
	// (u(ia{sv}av))

	// "No such object path '/NO_DBUSMENU'"
	// generated by QBittorrent when it sends a broken trayitem on startup
	// and replaces it later
	if (err && g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_OBJECT)) {
		g_error_free(err);
		g_object_unref(dbusmenu);
		return;
	} else if (err) {
		g_warning("%s\n", err->message);
		g_error_free(err);
		g_object_unref(dbusmenu);
		return;
	}

	uint32_t revision = 0;
	GVariant *layout;
	GVariant *menuitems;

	g_variant_get_child(retvariant, 0, "u", &revision);

	layout = g_variant_get_child_value(retvariant, 1);
	menuitems = g_variant_get_child_value(layout, 2);

	GMenu *menu = create_menumodel(menuitems, dbusmenu);
	sn_item_set_menu_model(dbusmenu->snitem, menu);

	g_object_unref(menu);
	g_variant_unref(menuitems);
	g_variant_unref(layout);
	g_variant_unref(retvariant);
	g_object_unref(dbusmenu);
}

static void
sn_dbusmenu_get_property(GObject *object, uint property_id, GValue *value, GParamSpec *pspec)
{
	SnDbusmenu *self = SN_DBUSMENU(object);

	switch (property_id) {
		case PROP_PROXY:
			g_value_set_object(value, self->proxy);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
	}
}

static void
sn_dbusmenu_set_property(GObject *object, uint property_id, const GValue *value, GParamSpec *pspec)
{
	SnDbusmenu *self = SN_DBUSMENU(object);

	switch (property_id) {
		case PROP_BUSNAME:
			self->busname = g_strdup(g_value_get_string(value));
			break;
		case PROP_BUSOBJ:
			self->busobj = g_strdup(g_value_get_string(value));
			break;
		case PROP_SNITEM:
			self->snitem = g_value_get_object(value);
			break;
		case PROP_PROXY:
			self->proxy = g_value_get_object(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
	}
}

static void
sn_dbusmenu_class_init(SnDbusmenuClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	obj_properties[PROP_BUSNAME] =
		g_param_spec_string("busname", NULL, NULL,
		                    NULL,
		                    G_PARAM_CONSTRUCT_ONLY |
		                    G_PARAM_WRITABLE |
		                    G_PARAM_STATIC_STRINGS);
	obj_properties[PROP_BUSOBJ] =
		g_param_spec_string("busobj", NULL, NULL,
		                    NULL,
		                    G_PARAM_CONSTRUCT_ONLY |
		                    G_PARAM_WRITABLE |
		                    G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_SNITEM] =
		g_param_spec_object("snitem", NULL, NULL,
		                    SN_TYPE_ITEM,
		                    G_PARAM_CONSTRUCT_ONLY |
		                    G_PARAM_WRITABLE |
		                    G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_PROXY] =
		g_param_spec_object("proxy", NULL, NULL,
		                    G_TYPE_DBUS_PROXY,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);


	object_class->set_property = sn_dbusmenu_set_property;
	object_class->get_property = sn_dbusmenu_get_property;

	object_class->constructed = sn_dbusmenu_constructed;
	object_class->dispose = sn_dbusmenu_dispose;
	object_class->finalize = sn_dbusmenu_finalize;

	g_object_class_install_properties(object_class, N_PROPERTIES, obj_properties);
}

static void
menuproxy_ready_cb(GObject *obj, GAsyncResult *res, void *data)
{
	SnDbusmenu *self = SN_DBUSMENU(data);

	GError *err = NULL;
	GDBusProxy *proxy = g_dbus_proxy_new_for_bus_finish(res, &err);

	if (err) {
		g_warning("Failed to construct gdbusproxy for menu: %s\n", err->message);
		g_error_free(err);
		g_object_unref(self);
		return;
	}

	g_debug("Created gdbusproxy for menu %s %s",
	        g_dbus_proxy_get_name(proxy),
	        g_dbus_proxy_get_object_path(proxy));

	g_object_set(self, "proxy", proxy, NULL);

	g_dbus_proxy_call(self->proxy,
			  "GetLayout",
			  g_variant_new ("(iias)", 0, -1, NULL),
			  G_DBUS_CALL_FLAGS_NONE,
			  -1,
			  NULL,
			  (GAsyncReadyCallback)on_menulayout_ready,
			  g_object_ref(self));

	g_signal_connect(self->proxy, "g-signal", G_CALLBACK(on_menuproxy_signal), self);
	g_object_unref(self);
}


static void
sn_dbusmenu_init(SnDbusmenu *self)
{
}

static void
sn_dbusmenu_constructed(GObject *obj)
{
	SnDbusmenu *self = SN_DBUSMENU(obj);

	GDBusNodeInfo *nodeinfo = g_dbus_node_info_new_for_xml(DBUSMENU_XML, NULL);
	g_dbus_proxy_new_for_bus(G_BUS_TYPE_SESSION,
	                         G_DBUS_PROXY_FLAGS_NONE,
	                         nodeinfo->interfaces[0],
	                         self->busname,
	                         self->busobj,
	                         "com.canonical.dbusmenu",
	                         NULL,
	                         (GAsyncReadyCallback)menuproxy_ready_cb,
	                         g_object_ref(self));
	g_dbus_node_info_unref(nodeinfo);

	G_OBJECT_CLASS(sn_dbusmenu_parent_class)->constructed(obj);
}

static void
sn_dbusmenu_dispose(GObject *obj)
{
	SnDbusmenu *self = SN_DBUSMENU(obj);

	g_debug("Disposing sndbusmenu %s %s",
	        self->busname,
	        self->busobj);

	g_object_unref(self->proxy);
	g_object_unref(self->snitem);

	G_OBJECT_CLASS(sn_dbusmenu_parent_class)->dispose(obj);
}

static void
sn_dbusmenu_finalize(GObject *obj)
{
	SnDbusmenu *self = SN_DBUSMENU(obj);
	g_free(self->busname);
	g_free(self->busobj);
	G_OBJECT_CLASS(sn_dbusmenu_parent_class)->finalize(obj);
}

GDBusProxy*
sn_dbusmenu_get_proxy(SnDbusmenu *self)
{
	GDBusProxy *proxy;
	g_object_get(self, "proxy", &proxy, NULL);

	return proxy;
}

SnDbusmenu*
sn_dbusmenu_new(const char *busname, const char *busobj, SnItem *snitem)
{
	return g_object_new(SN_TYPE_DBUSMENU,
	                    "busname", busname,
	                    "busobj", busobj,
	                    "snitem", snitem,
	                    NULL);
}
