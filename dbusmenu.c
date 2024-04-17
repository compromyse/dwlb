#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "dwlbtray.h"


static GMenu* create_menumodel(GVariant *data, StatusNotifierItem *snitem);


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
actioncbdata_finalize(ActionCallbackData *data, GClosure *closure)
{
	g_free(data);
	data = NULL;
}


static GSimpleAction*
create_action(uint32_t id, StatusNotifierItem *snitem)
{
	char *action_name = g_strdup_printf("%u", id);
	GSimpleAction *action = g_simple_action_new(action_name, NULL);

	ActionCallbackData *data = g_malloc(sizeof(ActionCallbackData));
	data->id = id;
	data->proxy = snitem->menuproxy;

	g_signal_connect_data(action,
	                      "activate",
	                      G_CALLBACK(action_activated_cb),
	                      data,
	                      (GClosureNotify)actioncbdata_finalize,
	                      G_CONNECT_DEFAULT);

	g_free(action_name);

	return action;
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


static gboolean
check_menuitem_visible(GVariant *data)
{
	gboolean isvisible = TRUE;
	GVariant *menu_data = g_variant_get_child_value(data, 1);
	g_variant_lookup(menu_data, "visible", "b", &isvisible);

	g_variant_unref(menu_data);

	return isvisible;
}


static GMenuItem*
create_menuitem(GVariant *data, StatusNotifierItem *snitem)
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
		GSimpleAction *action = create_action(id, snitem);
		char *action_name = g_strdup_printf("%s.%u", "menuitem", id);
		g_action_map_add_action(G_ACTION_MAP(snitem->actiongroup),
		                        G_ACTION(action));
		menuitem = g_menu_item_new(label, action_name);

		g_free(action_name);
		g_object_unref(action);

	} else if ((label && !(type && strcmp(type, "separator") == 0))) {
		GSimpleAction *action = create_action(id, snitem);
		g_simple_action_set_enabled(action, FALSE);
		char *action_name = g_strdup_printf("%s.%u", "menuitem", id);
		g_action_map_add_action(G_ACTION_MAP(snitem->actiongroup),
		                        G_ACTION(action));
		menuitem = g_menu_item_new(label, action_name);

		g_free(action_name);
		g_object_unref(action);
	}

	if (has_submenu) {
		GVariant *submenu_data = g_variant_get_child_value(data, 2);
		GMenu *submenu = create_menumodel(submenu_data, snitem);
		g_menu_item_set_submenu(menuitem, G_MENU_MODEL(submenu));
		g_object_unref(submenu);
		g_variant_unref(submenu_data);
	}

	g_variant_unref(menu_data);

	return menuitem;
}


static GMenu*
create_menumodel(GVariant *data, StatusNotifierItem *snitem)
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

			GMenuItem *menuitem = create_menuitem(menuitem_data, snitem);
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
			GMenuItem *menuitem = create_menuitem(menuitem_data, snitem);
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
on_menulayout_ready(GDBusProxy *proxy, GAsyncResult *res, StatusNotifierItem *snitem)
{
	GError *err = NULL;
	GVariant *data = g_dbus_proxy_call_finish(proxy, res, &err);
	// (u(ia{sv}av))

	// "No such object path '/NO_DBUSMENU'"
	// generated by QBittorrent when it sends a broken trayitem on startup
	// and replaces it later
	if (err && g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_OBJECT)) {
		g_error_free(err);
		g_bit_unlock(&snitem->lock, 0);
		return;
	} else if (err) {
		g_warning("%s\n", err->message);
		g_error_free(err);
		g_bit_unlock(&snitem->lock, 0);
		return;
	}

	uint32_t revision = 0;
	GVariant *layout;
	GVariant *menuitems;

	g_variant_get_child(data, 0, "u", &revision);

	layout = g_variant_get_child_value(data, 1);
	menuitems = g_variant_get_child_value(layout, 2);

	GMenu *menu = create_menumodel(menuitems, snitem);
	GtkWidget *popovermenu = gtk_popover_menu_new_from_model(NULL);
	gtk_popover_set_has_arrow(GTK_POPOVER(popovermenu), FALSE);
	gtk_popover_menu_set_menu_model(GTK_POPOVER_MENU(popovermenu), G_MENU_MODEL(menu));
	gtk_widget_set_parent(popovermenu, snitem->icon);

	snitem->popovermenu = popovermenu;

	g_object_unref(menu);
	g_variant_unref(menuitems);
	g_variant_unref(layout);
	g_variant_unref(data);

	g_bit_unlock(&snitem->lock, 0);
}


static void
on_layout_updated(GDBusProxy *proxy, GAsyncResult *res, StatusNotifierItem *snitem)
{
	GError *err = NULL;
	GVariant *data = g_dbus_proxy_call_finish(proxy, res, &err);

	// Errors which might occur when the tray is running slowly (eg under valgrind)
	// and user is spam clicking already exited icons

	// "No such object path '/MenuBar'
	if (err && g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_OBJECT)) {
		g_error_free(err);
		return;

	// "The name is not activatable"
	} else if (err && g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN)) {
		g_error_free(err);
		return;

	// "Remote peer disconnected"
	} else if (err && g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_NO_REPLY)) {
		g_error_free(err);
		return;

	} else if (err) {
		g_warning("%s\n", err->message);
		g_error_free(err);
		return;
	}

	GVariant *layout;
	GVariant *menuitems;

	layout = g_variant_get_child_value(data, 1);
	menuitems = g_variant_get_child_value(layout, 2);


	if (snitem && snitem->icon && GTK_IS_WIDGET(snitem->icon) &&
			snitem->popovermenu && GTK_IS_WIDGET(snitem->popovermenu)) {
		GMenu *newmenu = create_menumodel(menuitems, snitem);

		gtk_popover_menu_set_menu_model(GTK_POPOVER_MENU(snitem->popovermenu), G_MENU_MODEL(newmenu));
	}

	g_variant_unref(menuitems);
	g_variant_unref(layout);
	g_variant_unref(data);
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
                    StatusNotifierItem *snitem)
{
	if (strcmp(signal, "LayoutUpdated") == 0) {
		uint32_t revision = UINT32_MAX;
		int32_t parentid;
		g_variant_get(params, "(ui)", &revision, &parentid);
		if (snitem->menurevision != UINT32_MAX && revision <= snitem->menurevision) {
			// g_debug("%s got %s, but menurevision didn't change. Ignoring\n", snitem->busname, signal);
			return;
		} else if (!snitem || !snitem->icon || !GTK_IS_WIDGET(snitem->icon) ||
		           !snitem->popovermenu || !GTK_IS_WIDGET(snitem->popovermenu)) {
			// g_debug("%s got %s, but menu was already in destruction. Ignoring\n", snitem->busname, signal);
			return;
		} else {
			snitem->menurevision = revision;
		}

		g_dbus_proxy_call(snitem->menuproxy,
		                  "GetLayout",
		                  g_variant_new("(iias)", 0, -1, NULL),
		                  G_DBUS_CALL_FLAGS_NONE,
		                  -1,
		                  NULL,
		                  (GAsyncReadyCallback)on_layout_updated,
		                  snitem);

	} else if (strcmp(signal, "ItemsPropertiesUpdated") == 0) {
		if (!snitem || !snitem->icon || !GTK_IS_WIDGET(snitem->icon) ||
		    !snitem->popovermenu || !GTK_IS_WIDGET(snitem->popovermenu)) {
			// g_debug("Menu was already in destruction\n");
			return;
		}
		g_dbus_proxy_call(snitem->menuproxy,
		                  "GetLayout",
		                  g_variant_new("(iias)", 0, -1, NULL),
		                  G_DBUS_CALL_FLAGS_NONE,
		                  -1,
		                  NULL,
		                  (GAsyncReadyCallback)on_layout_updated,
		                  snitem);
	}
}


void
create_menu(GObject *obj, GAsyncResult *res, StatusNotifierItem *snitem)
{
	GError *err = NULL;
	GDBusProxy *proxy = g_dbus_proxy_new_for_bus_finish(res, &err);
	snitem->menuproxy = proxy;
	snitem->menurevision = UINT32_MAX;

	if (err) {
		g_warning("%s\n", err->message);
		g_error_free(err);
		g_bit_unlock(&snitem->lock, 0);
		return;
	}

	g_dbus_proxy_call(proxy,
	                  "GetLayout",
	                  g_variant_new ("(iias)", 0, -1, NULL),
	                  G_DBUS_CALL_FLAGS_NONE,
	                  -1,
	                  NULL,
	                  (GAsyncReadyCallback)on_menulayout_ready,
	                  snitem);

	g_signal_connect(proxy,
	                "g-signal",
	                G_CALLBACK (on_menuproxy_signal),
	                snitem);
}
