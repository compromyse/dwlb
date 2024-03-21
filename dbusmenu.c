#include <time.h>

#include <glib.h>
#include <gio/gio.h>

#include "dwlbtray.h"

static GMenu* create_menumodel(GVariant *data, StatusNotifierItem *snitem);


static void
action_activated_cb(GSimpleAction *action, GVariant* param, ActionCallbackData *data)
{
	// GError *err = NULL;

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
	/*
	GVariant *retval = g_dbus_proxy_call_sync(data->proxy,
	                                          "Event",
	                                          g_variant_new("(isvu)",
	                                                        data->id,
	                                                        "clicked",
	                                                        g_variant_new_string(""),
	                                                        time(NULL)),
	                                          G_DBUS_CALL_FLAGS_NONE,
	                                          -1,
	                                          NULL,
	                                          &err);

	if (err) {
		fprintf(stderr, "%s\n", err->message);
		g_error_free(err);
	} else {
		g_variant_unref(retval);
	}
	*/
}


static GSimpleAction*
create_action(uint32_t id, StatusNotifierItem *snitem)
{
	char *action_name = g_strdup_printf("%u", id);
	GSimpleAction *action = g_simple_action_new(action_name, NULL);

	ActionCallbackData *data = g_malloc(sizeof(ActionCallbackData));
	data->id = id;
	data->proxy = snitem->menuproxy;
	snitem->action_cb_data_slist = g_slist_prepend(snitem->action_cb_data_slist, data);

	g_signal_connect(action, "activate", G_CALLBACK(action_activated_cb), data);

	g_free(action_name);

	return action;
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

	/* TODO: dynamic menu updates
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
	/* TODO: dynamic menu updates
	 * g_variant_dict_lookup(&dict, "toggle-type", "&s", &toggle_type);
	 * g_variant_dict_lookup(&dict, "toggle-state", "i", &toggle_state);
	 */
	g_variant_dict_clear(&dict);

	if (has_submenu_s && strcmp(has_submenu_s, "submenu") == 0)
		has_submenu = TRUE;

	/* TODO: dynamic menu updates
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
	} else if ((label && (!isvisible || !isenabled)) && !(type && strcmp(type, "separator") == 0)) {
		if (!isvisible)
			g_debug("menuitem %i, label %s should be invisible\n", id, label);
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
	GMenu *ret = NULL;

	GVariantIter iter;
	GVariant *menuitem_data;
	gboolean has_sections = FALSE;
	GMenu *menu = g_menu_new();
	GMenu *section = g_menu_new();
	g_variant_iter_init(&iter, data);

	while ((g_variant_iter_next(&iter, "v", &menuitem_data))) {
		GMenuItem *menuitem = create_menuitem(menuitem_data, snitem);
		if (menuitem) {
			g_menu_append_item(section, menuitem);
			g_object_unref(menuitem);
		}
		else {
			g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
			g_object_unref(section);
			has_sections = TRUE;
			section = g_menu_new();
		}

		g_variant_unref(menuitem_data);
	}

	if (has_sections) {
		g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
		ret = menu;
		g_object_unref(section);
	} else {
		ret = section;
		g_object_unref(menu);
	}

	return ret;
}


static void
on_menulayout_ready(GDBusProxy *proxy, GAsyncResult *res, StatusNotifierItem *snitem)
{
	GError *err = NULL;
	GVariant *data = g_dbus_proxy_call_finish(proxy, res, &err);
	// (u(ia{sv}av))

	if (err) {
		// Sometimes apps send update messages just as they are closing
		// So the dbus object might not exist in this case
		g_debug("%s\n", err->message);
		g_error_free(err);

		return;
	}


	uint32_t revision;
	GVariant *layout;
	// (ia{sv}av)
	// uint32_t            GVariant *                             GVariant *
	//    0,     {"children-display": "submenu"}, {GVariant *menuitem, GVariant *menuitem ...}
	GVariant *menuitems;
	// (ia{sv}av)
	// uint32_t            GVariant *                             GVariant *
	//    1,     {"label": "foobar", ...},        {GVariant *menuitem, GVariant *menuitem ...}

	g_variant_get_child(data, 0, "u", &revision);
	if (snitem->menurevision != UINT32_MAX && revision <= snitem->menurevision) {
		g_variant_unref(data);
		return;
	} else {
		snitem->menurevision = revision;
	}

	if (snitem->menu && snitem->popovermenu) {
		gtk_widget_unparent(snitem->popovermenu);
		g_menu_remove_all(snitem->menu);
	}
	layout = g_variant_get_child_value(data, 1);

	menuitems = g_variant_get_child_value(layout, 2);

	GMenu *menu = create_menumodel(menuitems, snitem);
	snitem->menu = menu;

	snitem->popovermenu = gtk_popover_menu_new_from_model(G_MENU_MODEL(snitem->menu));
	gtk_popover_set_has_arrow(GTK_POPOVER(snitem->popovermenu), FALSE);
	gtk_popover_menu_set_menu_model(GTK_POPOVER_MENU(snitem->popovermenu),
	                                 G_MENU_MODEL(snitem->menu));
	gtk_widget_set_parent(snitem->popovermenu, snitem->icon);

	g_variant_unref(menuitems);
	g_variant_unref(layout);
	g_variant_unref(data);
}


static void
on_menuproxy_signal(GDBusProxy *proxy,
                    const char *sender,
                    const char *signal,
                    GVariant *params,
                    StatusNotifierItem *snitem)
{
	if (strcmp(signal, "LayoutUpdated") == 0) {
		g_debug("%s's menu got LayoutUpdated\n", snitem->busname);
		g_dbus_proxy_call(snitem->menuproxy,
		                  "GetLayout",
		                  g_variant_new("(iias)", 0, -1, NULL),
		                  G_DBUS_CALL_FLAGS_NONE,
		                  -1,
		                  NULL,
		                  (GAsyncReadyCallback)on_menulayout_ready,
		                  snitem);
	// TODO: dynamic menu updates
	} else if (strcmp(signal, "ItemsPropertiesUpdated") == 0) {
		g_debug("%s's menu got LayoutUpdated\n", snitem->busname);
	}
}


void
create_menu(GDBusProxy *proxy, GAsyncResult *res, StatusNotifierItem *snitem)
{
	GError *err = NULL;
	snitem->menuproxy = g_dbus_proxy_new_for_bus_finish(res, &err);
	snitem->menurevision = UINT32_MAX;

	if (err) {
		g_debug("%s\n", err->message);
		g_error_free(err);
		return;
	}

	g_dbus_proxy_call(snitem->menuproxy,
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
