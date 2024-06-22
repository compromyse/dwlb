#include "sndbusmenu.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "snitem.h"


struct _SnDbusmenu {
	GObject parent_instance;

	GMutex mutex;

	char* busname;
	char* busobj;
	SnItem* snitem;

	GMenu* menu;
	GDBusProxy* proxy;

	uint32_t revision;
	gboolean reschedule;
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

enum
{
	ABOUT_TO_SHOW_HANDLED,
	LAST_SIGNAL
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };
static uint signals[LAST_SIGNAL];

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

static GMenuItem*	create_menuitem			(int32_t id, GVariant *menu_data,
							GVariant *submenu_data,
							SnDbusmenu *self);


static void
action_activated_handler(GSimpleAction *action, GVariant* param, ActionCallbackData *data)
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
	                      G_CALLBACK(action_activated_handler),
	                      data,
	                      action_free,
	                      G_CONNECT_DEFAULT);

	g_free(action_name);

	return action;
}

static GMenuItem*
create_menuitem(int32_t id, GVariant *menuitem_data, GVariant *submenuitem_data, SnDbusmenu *self)
{
	// a{sv]
	// GVariant *data
	GMenuItem *menuitem = NULL;


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
	g_variant_dict_init(&dict, menuitem_data);
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

	} else if ((label && isvisible && !isenabled && !(type && strcmp(type, "separator") == 0))) {
		GSimpleAction *action = create_action(id, self);
		g_simple_action_set_enabled(action, FALSE);
		char *action_name = g_strdup_printf("%s.%u", "menuitem", id);
		sn_item_add_action(self->snitem, action);
		menuitem = g_menu_item_new(label, action_name);

		g_free(action_name);
		g_object_unref(action);
	}

	if (isvisible && has_submenu) {
		GMenu *submenu = create_menumodel(submenuitem_data, self);
		g_menu_item_set_submenu(menuitem, G_MENU_MODEL(submenu));
		g_object_unref(submenu);
	}

	if (menuitem)
		g_menu_item_set_attribute(menuitem, "itemid", "i", id);

	return menuitem;
}

static GMenu*
create_menumodel(GVariant *data, SnDbusmenu *self)
{
	GMenu *ret = g_menu_new();
	GVariantIter iter;

	//  (ia{sv}av)
	GVariant *menuitem_data_packed;
	GVariant *menuitem_data;
	int32_t id;

	g_variant_iter_init(&iter, data);
	while ((g_variant_iter_next(&iter, "v", &menuitem_data_packed))) {
		g_variant_get_child(menuitem_data_packed, 0, "i", &id);
		menuitem_data = g_variant_get_child_value(menuitem_data_packed, 1);
		GVariant *submenu_data = g_variant_get_child_value(menuitem_data_packed, 2);
		GMenuItem *menuitem = create_menuitem(id, menuitem_data, submenu_data, self);
		if (menuitem) {
			g_menu_append_item(ret, menuitem);
			g_object_unref(menuitem);
		}
		g_variant_unref(submenu_data);
		g_variant_unref(menuitem_data);
		g_variant_unref(menuitem_data_packed);
	}

	return ret;
}

static void
layout_update(SnDbusmenu *self)
{
	GError *err = NULL;

	GVariant *data = g_dbus_proxy_call_sync(self->proxy,
						"GetLayout",
						g_variant_new("(iias)", 0, -1, NULL),
						G_DBUS_CALL_FLAGS_NONE,
						-1,
						NULL,
						&err);

	if (err) {
		g_debug("Error in layout_update %s", err->message);
		g_error_free(err);
		return;
	}

	GVariant *layout;
	GVariant *menuitems;

	layout = g_variant_get_child_value(data, 1);
	menuitems = g_variant_get_child_value(layout, 2);


	gboolean isvisible = sn_item_get_popover_visible(self->snitem);
	if (isvisible) {
		self->reschedule = TRUE;
		g_debug("Popover was visible, couldn't update menu %s", self->busname);
	} else {
		g_object_unref(self->menu);
		sn_item_remove_all_actions(self->snitem);
		self->menu = create_menumodel(menuitems, self);
		sn_item_set_menu_model(self->snitem, self->menu);
	}
	g_variant_unref(menuitems);
	g_variant_unref(layout);

	g_variant_unref(data);
}

static void
reschedule_update(SnItem *snitem, GParamSpec *pspec, void *data)
{
	SnDbusmenu *self = SN_DBUSMENU(data);

	gboolean popover_visible = sn_item_get_popover_visible(snitem);
	if (popover_visible || !self->reschedule)
		return;

	self->reschedule = FALSE;

	layout_update(self);
}

static gboolean
find_menuid(GMenuModel *menu, int id, GVariant *menu_data, SnDbusmenu *self)
{
	gboolean ret = FALSE;

	g_assert(G_IS_MENU_MODEL(menu));
	int len = g_menu_model_get_n_items(menu);

	int idattr;
	for (int i = 0; i < len; i++) {
		GMenuModel *submenu = g_menu_model_get_item_link(menu, i, G_MENU_LINK_SUBMENU);
		if (submenu) {
			ret = find_menuid(submenu, id, menu_data, self);
			g_object_unref(submenu);
		}

		if (!ret) {
			g_menu_model_get_item_attribute(menu, i, "itemid", "i", &idattr);
			if (idattr == id) {
				GMenuModel *link = g_menu_model_get_item_link(menu, i, G_MENU_LINK_SUBMENU);
				g_menu_remove(G_MENU(menu), i);
				if (menu_data) {
					GMenuItem *item = create_menuitem(id, menu_data, NULL, self);
					if (item && link)
						g_menu_item_set_link(item, G_MENU_LINK_SUBMENU, link);
					if (item) {
						g_menu_insert_item(G_MENU(menu), i, item);
						g_object_unref(item);
					}
				}
				if (link)
					g_object_unref(link);

				ret = TRUE;
				break;
			}
		}
	}

	return ret;
}

static void
props_removed_handler(GVariant *props,  SnDbusmenu *self)
{
	// a(ias)
	GVariant *data;
	GMenuModel *menu = G_MENU_MODEL(self->menu);
	int id;

	GVariantIter iter;
	g_variant_iter_init(&iter, props);
	while ((data = g_variant_iter_next_value(&iter))) {
		g_variant_get_child(data, 0, "i", &id);
		find_menuid(menu, id, NULL, self);
		char *action_name = g_strdup_printf("menuitem.%i", id);
		sn_item_remove_action(self->snitem, action_name);
		g_free(action_name);
		g_variant_unref(data);
	}
}

static void
props_updated_handler(GVariant *props,  SnDbusmenu *self)
{
	// a(ia{sv})
	GVariant *data;
	GMenuModel *menu = G_MENU_MODEL(self->menu);
	int id;
	GVariant *menuitem_data;

	GVariantIter iter;
	g_variant_iter_init(&iter, props);
	while ((data = g_variant_iter_next_value(&iter))) {
		g_variant_get_child(data, 0, "i", &id);
		menuitem_data = g_variant_get_child_value(data, 1);
		find_menuid(menu, id, menuitem_data, self);
		g_variant_unref(menuitem_data);
		g_variant_unref(data);
	}
}

static void
proxy_signal_handler(GDBusProxy *proxy,
                     const char *sender,
                     const char *signal,
                     GVariant *params,
                     void *data)
{
	SnDbusmenu *self = SN_DBUSMENU(data);
	g_mutex_lock(&self->mutex);

	if (strcmp(signal, "LayoutUpdated") == 0) {
		gboolean popover_visible = sn_item_get_popover_visible(self->snitem);

		uint32_t revision;
		int32_t parentid;
		g_variant_get(params, "(ui)", &revision, &parentid);
		g_debug("%s got LayoutUpdated, revision %i, parentid %i", self->busname, revision, parentid);
		if (!popover_visible && (self->revision == UINT32_MAX || self->revision < revision)) {
			layout_update(self);
			self->revision = revision;
		} else if (popover_visible) {
			self->reschedule = TRUE;
		}

	} else if (strcmp(signal, "ItemsPropertiesUpdated") == 0) {
		g_debug("%s got ItemsPropertiesUpdated", self->busname);
		GVariant *removed_props = g_variant_get_child_value(params, 1);
		GVariant *updated_props = g_variant_get_child_value(params, 0);

		if (removed_props) {
			props_removed_handler(removed_props, self);
			g_variant_unref(removed_props);
		}
		if (updated_props) {
			props_updated_handler(updated_props, self);
			g_variant_unref(updated_props);
		}
	}
	g_mutex_unlock(&self->mutex);
}

static void
menulayout_ready_handler(GObject *obj, GAsyncResult *res, void *data)
{
	SnDbusmenu *self = SN_DBUSMENU(data);

	GError *err = NULL;
	GVariant *retvariant = g_dbus_proxy_call_finish(self->proxy, res, &err);
	// (u(ia{sv}av))

	// "No such object path '/NO_DBUSMENU'"
	// generated by QBittorrent when it sends a broken trayitem on startup
	// and replaces it later
	if (err && g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_OBJECT)) {
		g_error_free(err);
		g_object_unref(self);
		return;
	} else if (err) {
		g_warning("%s\n", err->message);
		g_error_free(err);
		g_object_unref(self);
		return;
	}

	uint32_t revision = 0;
	GVariant *layout;
	GVariant *menuitems;


	g_variant_get_child(retvariant, 0, "u", &revision);

	layout = g_variant_get_child_value(retvariant, 1);
	menuitems = g_variant_get_child_value(layout, 2);

	self->menu = create_menumodel(menuitems, self);
	sn_item_set_menu_model(self->snitem, self->menu);

	g_variant_unref(menuitems);
	g_variant_unref(layout);
	g_variant_unref(retvariant);
	g_object_unref(self);
}

static void
about_to_show_timeout_handler(void *data)
{
	SnDbusmenu *self = SN_DBUSMENU(data);

	g_signal_emit(self, signals[ABOUT_TO_SHOW_HANDLED], 0);
	g_object_unref(self);
}

static void
about_to_show_handler(GObject *obj, GAsyncResult *res, void *data)
{
	SnDbusmenu *self = SN_DBUSMENU(data);

	GError *err = NULL;
	GVariant *val =  g_dbus_proxy_call_finish(self->proxy, res, &err);

	// Discord generates the following error here:
	// 'G_DBUS_ERROR' 'G_DBUS_ERROR_FAILED' 'error occurred in AboutToShow'
	// We ignore it.
	if (err && !g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_FAILED) &&
	    g_strrstr(err->message, "error occured in AboutToShow") != 0) {
		g_warning("%s\n", err->message);

	} else {
		// This dbusmenu call might have triggered a menu update,
		// give it a chance to finish. nm-applet update takes 60ms, give 100ms.
		g_timeout_add_once(100, about_to_show_timeout_handler, g_object_ref(self));
	}

	err ? g_error_free(err) : g_variant_unref(val);
	g_object_unref(self);
}


static void
rightclick_handler(GObject *obj, void *data)
{
	SnDbusmenu *self = SN_DBUSMENU(data);

	g_assert(SN_IS_DBUSMENU(self));
	g_dbus_proxy_call(self->proxy,
	                  "AboutToShow",
	                  g_variant_new("(i)", 0),
	                  G_DBUS_CALL_FLAGS_NONE,
	                  -1,
	                  NULL,
	                  about_to_show_handler,
	                  g_object_ref(self));
}

static void
proxy_ready_handler(GObject *obj, GAsyncResult *res, void *data)
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
			  menulayout_ready_handler,
			  g_object_ref(self));

	g_signal_connect(self->proxy, "g-signal", G_CALLBACK(proxy_signal_handler), self);
	g_object_unref(self);
}

static void
sn_dbusmenu_get_property(GObject *object, uint property_id, GValue *value, GParamSpec *pspec)
{
	G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
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

	object_class->set_property = sn_dbusmenu_set_property;
	object_class->get_property = sn_dbusmenu_get_property;
	object_class->constructed = sn_dbusmenu_constructed;
	object_class->dispose = sn_dbusmenu_dispose;
	object_class->finalize = sn_dbusmenu_finalize;

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
		                    G_PARAM_WRITABLE |
		                    G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties(object_class, N_PROPERTIES, obj_properties);

	signals[ABOUT_TO_SHOW_HANDLED] = g_signal_new("abouttoshowhandled",
	                                              SN_TYPE_DBUSMENU,
	                                              G_SIGNAL_RUN_LAST,
	                                              0,
	                                              NULL,
	                                              NULL,
	                                              NULL,
	                                              G_TYPE_NONE,
	                                              0);
}

static void
sn_dbusmenu_init(SnDbusmenu *self)
{
	// When reschedule is TRUE, menu will be updated next time it is closed.
	self->reschedule = FALSE;
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
	                         (GAsyncReadyCallback)proxy_ready_handler,
	                         g_object_ref(self));
	g_dbus_node_info_unref(nodeinfo);

	g_signal_connect(self->snitem, "notify::menuvisible", G_CALLBACK(reschedule_update), self);

	g_signal_connect(self->snitem, "rightclick", G_CALLBACK(rightclick_handler), self);

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
	if (self->menu)
		g_object_unref(self->menu);
	g_free(self->busname);
	g_free(self->busobj);
	G_OBJECT_CLASS(sn_dbusmenu_parent_class)->finalize(obj);
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
