#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>

#include "dwlbtray.h"


static void
argb_to_rgba(int32_t width, int32_t height, unsigned char *icon_data)
{
	// Icon data is ARGB, gdk textures are RGBA. Flip the channels
	// Shamelessly copied from Waybar
	for (int32_t i = 0; i < 4 * width * height; i += 4) {
	        unsigned char alpha = icon_data[i];
	        icon_data[i] = icon_data[i + 1];
	        icon_data[i + 1] = icon_data[i + 2];
	        icon_data[i + 2] = icon_data[i + 3];
	        icon_data[i + 3] = alpha;
	}
}


static void
about_to_show_cb(GDBusProxy *proxy, GAsyncResult *res, StatusNotifierItem *snitem)
{
	GError *err = NULL;
	GVariant *retval = g_dbus_proxy_call_finish(proxy, res, &err);

	if (err) {
		// Some apps require "AboutToShow" to be called before activating a menuitem
		// Others do not implement this method
		// In case of the latter we log a debug warning and continue
		g_debug("%s\n", err->message);
		g_error_free(err);
	} else {
		g_variant_unref(retval);
	}

	// Widget may be about to be destroyed
	if (GTK_IS_WIDGET(snitem->popovermenu))
		gtk_popover_popup(GTK_POPOVER(snitem->popovermenu));
}


static void
on_leftclick_cb(GtkGestureClick *click,
                int n_press,
                double x,
                double y,
                StatusNotifierItem *snitem)
{
	g_dbus_proxy_call(snitem->proxy,
			  "Activate",
			  g_variant_new("(ii)", 0, 0),
			  G_DBUS_CALL_FLAGS_NONE,
			  -1,
			  NULL,
			  NULL,
			  snitem);
}




static void
on_rightclick_cb(GtkGestureClick *click,
                 int n_press,
                 double x,
                 double y,
                 StatusNotifierItem *snitem)
{
	g_dbus_proxy_call(snitem->menuproxy,
	                  "AboutToShow",
	                  g_variant_new("(i)", 0),
	                  G_DBUS_CALL_FLAGS_NONE,
	                  -1,
	                  NULL,
	                  (GAsyncReadyCallback)about_to_show_cb,
	                  snitem);
}


static void
pb_destroy(unsigned char *pixeld, void *data)
{
	g_free(pixeld);
}


static GVariant*
select_icon_by_size(GVariant *icondata_v)
{
	// Apps broadcast icons as variant a(iiay)
	// Meaning array of tuples, tuple representing an icon
	// first 2 members ii in each tuple are width and height
	// We iterate the array and pick the icon size closest to
	// the target based on its width and save the index
	GVariantIter iter;
	int selected_index = 0;
	int current_index = 0;
	int32_t target_icon_size = 22;
	int32_t diff = INT32_MAX;
	GVariant *child;
	g_variant_iter_init(&iter, icondata_v);
	while ((child = g_variant_iter_next_value(&iter))) {
		int32_t curwidth;
		g_variant_get_child(child, 0, "i", &curwidth);
		int32_t curdiff;
		if (curwidth > target_icon_size)
			curdiff = curwidth - target_icon_size;
		else
			curdiff = target_icon_size - curwidth;

		if (curdiff < diff)
			selected_index = current_index;

		current_index = current_index + 1;
		g_variant_unref(child);
	}

	GVariant *iconpixmap_v = g_variant_get_child_value(icondata_v,
	                                                   (size_t)selected_index);

	return iconpixmap_v;
}


static GdkPaintable*
get_paintable_from_data(GVariant *icondata_v)
{
	GdkPaintable *paintable;
	GVariantIter iter;
	GVariant *iconpixmap_v = select_icon_by_size(icondata_v);

	int32_t width;
	int32_t height;
	GVariant *icon_data_v;

	g_variant_iter_init(&iter, iconpixmap_v);

	g_variant_iter_next(&iter, "i", &width);
	g_variant_iter_next(&iter, "i", &height);
	icon_data_v = g_variant_iter_next_value(&iter);

	size_t size = g_variant_get_size(icon_data_v);
	const void *icon_data_dup = g_variant_get_data(icon_data_v);

	unsigned char *icon_data = g_memdup2(icon_data_dup, size);
	argb_to_rgba(width, height, icon_data);

	int32_t padding = size / height - 4 * width;
	int32_t rowstride = 4 * width + padding;

	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(icon_data,
	                                             GDK_COLORSPACE_RGB,
	                                             TRUE,
	                                             8,
	                                             width,
	                                             height,
	                                             rowstride,
	                                             (GdkPixbufDestroyNotify)pb_destroy,
	                                             NULL);

	GdkTexture *texture = gdk_texture_new_for_pixbuf(pixbuf);
	paintable = GDK_PAINTABLE(texture);

	g_object_unref(pixbuf);
	g_variant_unref(icon_data_v);
	g_variant_unref(iconpixmap_v);

	return paintable;
}


static GdkPaintable*
get_paintable_from_name(const char *iconname)
{
	GdkPaintable *paintable = NULL;
	GtkIconPaintable *icon;

	GtkIconTheme *theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
	icon = gtk_icon_theme_lookup_icon(theme,
	                                  iconname,
	                                  NULL,  // const char **fallbacks
	                                  22,
	                                  1,
	                                  GTK_TEXT_DIR_LTR,
	                                  0);  // GtkIconLookupFlags
	paintable = GDK_PAINTABLE(icon);

	return paintable;
}


static void
new_iconname_handler(GDBusProxy *proxy, GAsyncResult *res, StatusNotifierItem *snitem)
{
	GError *err = NULL;
	GVariant *data = g_dbus_proxy_call_finish(proxy, res, &err);
	// (s)

	if (err) {
		g_debug("%s\n", err->message);
		g_error_free(err);
		return;
	}

	GVariant *iconname_v;
	char *iconname = NULL;
	g_variant_get(data, "(v)", &iconname_v);
	g_variant_get(iconname_v, "s", &iconname);

	if (strcmp(iconname, snitem->iconname) == 0) {
		g_debug("%s\n", "pixmap didnt change, nothing to");
		g_variant_unref(iconname_v);
		g_variant_unref(data);
		return;
	}

	g_object_unref(snitem->paintable);
	snitem->iconname = iconname;
	snitem->paintable = get_paintable_from_name(snitem->iconname);
	gtk_image_set_from_paintable(GTK_IMAGE(snitem->icon), snitem->paintable);

	g_variant_unref(iconname_v);
	g_variant_unref(data);
}


static void
new_iconpixmap_handler(GDBusProxy *proxy, GAsyncResult *res, StatusNotifierItem *snitem)
{
	GError *err = NULL;
	GVariant *data = g_dbus_proxy_call_finish(proxy, res, &err);
	// (v)

	if (err) {
		g_debug("%s\n", err->message);
		g_error_free(err);
		return;
	}

	GVariant *newpixmap_v;
	g_variant_get(data, "(v)", &newpixmap_v);
	g_variant_unref(data);

	if (g_variant_equal(newpixmap_v, snitem->iconpixmap_v)) {
		g_debug ("%s\n", "iconname didnt change, nothing to");
		g_variant_unref(newpixmap_v);
		return;
	}

	g_object_unref(snitem->paintable);
	g_variant_unref(snitem->iconpixmap_v);
	snitem->iconpixmap_v = newpixmap_v;
	snitem->paintable = get_paintable_from_data(snitem->iconpixmap_v);
	gtk_image_set_from_paintable(GTK_IMAGE (snitem->icon), snitem->paintable);
}


static void
trayitem_signal_handler(GDBusProxy *proxy,
                        const char *sender,
                        const char *signal,
                        GVariant *data_v,
                        StatusNotifierItem *snitem)
{
	// TODO: this can fire many times in a short amount of time
	if (strcmp(signal, "NewIcon") == 0) {
		if (snitem->iconpixmap_v)
			g_dbus_proxy_call(proxy,
			                  "org.freedesktop.DBus.Properties.Get",
			                  g_variant_new("(ss)", "org.kde.StatusNotifierItem", "IconPixmap"),
			                  G_DBUS_CALL_FLAGS_NONE,
			                  -1,
			                  NULL,
			                  (GAsyncReadyCallback)new_iconpixmap_handler,
			                  snitem);

		if (snitem->iconname)
			g_dbus_proxy_call(proxy,
			                  "org.freedesktop.DBus.Properties.Get",
			                  g_variant_new("(ss)", "org.kde.StatusNotifierItem", "IconName"),
			                  G_DBUS_CALL_FLAGS_NONE,
			                  -1,
			                  NULL,
			                  (GAsyncReadyCallback) new_iconname_handler,
			                  snitem);
	}
}


static GtkWidget*
create_icon(GDBusProxy *proxy, StatusNotifierItem *snitem)
{

	GtkWidget *image = NULL;

	char *iconname = NULL;
	GdkPaintable *paintable = NULL;
	GVariant *iconname_v = g_dbus_proxy_get_cached_property(proxy, "IconName");

	if (iconname_v) {
		g_variant_get(iconname_v, "s", iconname);
		g_variant_unref(iconname_v);
	}

	if (iconname && strcmp(iconname, "") != 0) {
		paintable = get_paintable_from_name(iconname);

		snitem->iconname = iconname;

	} else {
		GVariant *iconpixmap_v = g_dbus_proxy_get_cached_property(proxy, "IconPixmap");
		if (!iconpixmap_v)
			return NULL;
		paintable = get_paintable_from_data(iconpixmap_v);

		snitem->iconpixmap_v = iconpixmap_v;

		if (iconname)
			g_free(iconname);
	}

	image = gtk_image_new_from_paintable(paintable);
	snitem->paintable = paintable;

	return image;
}


void
create_trayitem(GDBusConnection *conn, GAsyncResult *res, StatusNotifierItem *snitem)
{
	GError *err = NULL;
	snitem->proxy = g_dbus_proxy_new_finish(res, &err);

	if (err) {
		fprintf(stderr, "%s\n", err->message);
		g_error_free(err);
		return;
	}

	GtkIconTheme *theme = gtk_icon_theme_get_for_display(gdk_display_get_default ());
	GVariant *iconthemepath_v = g_dbus_proxy_get_cached_property(snitem->proxy, "IconThemePath");

	if (iconthemepath_v) {
		const char *path = g_variant_get_string(iconthemepath_v, NULL);
		gtk_icon_theme_add_search_path(theme, path);
		g_variant_unref(iconthemepath_v);
	}

	GtkGesture *leftclick = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(leftclick), 1);
	g_signal_connect(leftclick, "pressed", G_CALLBACK(on_leftclick_cb), snitem);

	GtkGesture *rightclick = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rightclick), 3);
	g_signal_connect(rightclick, "pressed", G_CALLBACK(on_rightclick_cb), snitem);

	const char *valid_menuobjects[] = {
		"/MenuBar",
		"/com/canonical/dbusmenu",
		"/org/ayatana/NotificationItem",
		NULL
	};

	GVariant *menuobj_v = g_dbus_proxy_get_cached_property(snitem->proxy, "Menu");
	if (menuobj_v) {
		snitem->menuobj = g_strdup(g_variant_get_string(menuobj_v, NULL));
		g_variant_unref(menuobj_v);
	} else {
		snitem->menuobj = g_strdup("Invalid_menuobj");
	}
	snitem->actiongroup = g_simple_action_group_new();

	char *xml_path = g_strdup_printf("%s%s", RESOURCE_PATH, "/DBusMenu.xml");
	snitem->menunodeinfo = get_interface_info(xml_path);
	g_free(xml_path);

	// gtk_popover_menu_set_menu_model(GTK_POPOVER_MENU(snitem->popovermenu),
	//                                 G_MENU_MODEL(snitem->menu));

	g_signal_connect(snitem->proxy, "g-signal", G_CALLBACK(trayitem_signal_handler), snitem);

	for (int i = 0; valid_menuobjects[i] != NULL; i++) {
		if (g_strrstr(snitem->menuobj, valid_menuobjects[i]) != NULL) {
			g_dbus_proxy_new_for_bus(G_BUS_TYPE_SESSION,
			                         G_DBUS_PROXY_FLAGS_NONE,
			                         snitem->menunodeinfo->interfaces[0],
			                         snitem->busname,
			                         snitem->menuobj,
			                         "com.canonical.dbusmenu",
			                         NULL,
			                         (GAsyncReadyCallback)create_menu,
			                         snitem);
		}
	}

	GtkWidget *image = create_icon(snitem->proxy, snitem);
	if (!image)
		return;

	snitem->icon = image;
	gtk_box_append(GTK_BOX(snitem->host->box), snitem->icon);

	gtk_widget_add_controller(snitem->icon, GTK_EVENT_CONTROLLER(leftclick));
	gtk_widget_add_controller(snitem->icon, GTK_EVENT_CONTROLLER(rightclick));
	gtk_widget_insert_action_group(snitem->icon,
	                               "menuitem",
	                               G_ACTION_GROUP (snitem->actiongroup));
}
