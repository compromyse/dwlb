#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

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
on_leftclick_cb(GtkGestureClick *click,
                int n_press,
                double x,
                double y,
                StatusNotifierItem *snitem)
{
	if (snitem && snitem->menuproxy && !snitem->isclosing)
		g_dbus_proxy_call(snitem->proxy,
				  "Activate",
				  g_variant_new("(ii)", 0, 0),
				  G_DBUS_CALL_FLAGS_NONE,
				  -1,
				  NULL,
				  NULL,
				  NULL);
}


static void
rightclick_validate(GDBusProxy *proxy, GAsyncResult *res, StatusNotifierItem *snitem)
{
	GError *err = NULL;
	GVariant *val =  g_dbus_proxy_call_finish(proxy, res, &err);

	// This error is generated when answer for the call arrives after
	// icon was finalized.
	if (err && g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_NO_REPLY)) {
		g_error_free(err);
		return;

	// Discord generates the following error here:
	// 'G_DBUS_ERROR' 'G_DBUS_ERROR_FAILED' 'error occurred in AboutToShow'
	// We ignore it.
	} else if (err && g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_FAILED) &&
		   g_strrstr(err->message, "error occured in AboutToShow") == 0) {
		g_error_free(err);

		if (snitem && snitem->icon && GTK_IS_WIDGET(snitem->icon) &&
		    snitem->popovermenu && GTK_IS_WIDGET(snitem->popovermenu))
			gtk_popover_popup(GTK_POPOVER(snitem->popovermenu));

	// Report rest of possible errors
	} else if (err) {
		g_warning("%s\n", err->message);
		g_error_free(err);

	} else {
		g_variant_unref(val);
		if (snitem && snitem->icon && GTK_IS_WIDGET(snitem->icon) &&
		    snitem->popovermenu && GTK_IS_WIDGET(snitem->popovermenu))
			gtk_popover_popup(GTK_POPOVER(snitem->popovermenu));
	}
}


static void
on_rightclick_cb(GtkGestureClick *click,
                 int n_press,
                 double x,
                 double y,
                 StatusNotifierItem *snitem)
{
	if (snitem && snitem->menuproxy && !snitem->isclosing) {
		if (snitem && snitem->icon && GTK_IS_WIDGET(snitem->icon) &&
		    snitem->popovermenu && GTK_IS_WIDGET(snitem->popovermenu))
			gtk_popover_popdown(GTK_POPOVER(snitem->popovermenu));

		g_dbus_proxy_call(snitem->menuproxy,
		                  "AboutToShow",
		                  g_variant_new("(i)", 0),
		                  G_DBUS_CALL_FLAGS_NONE,
		                  -1,
		                  NULL,
		                  (GAsyncReadyCallback)rightclick_validate,
		                  snitem);
	}
}


static void
pb_destroy(unsigned char *pixeld, void *data)
{
	g_free(pixeld);
}


static GVariant*
select_icon_by_size(GVariant *icondata_v, int target_size)
{
	// Apps broadcast icons as variant a(iiay)
	// Meaning array of tuples, tuple representing an icon
	// first 2 members ii in each tuple are width and height
	// We iterate the array and pick the icon size closest to
	// the target based on its width and save the index
	GVariantIter iter;
	int selected_index = 0;
	int current_index = 0;
	int32_t target_icon_size = (int32_t)target_size;
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
get_paintable_from_data(GVariant *icondata_v, int target_size)
{
	GdkPaintable *paintable;
	GVariantIter iter;
	GVariant *iconpixmap_v = select_icon_by_size(icondata_v, target_size);

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
get_paintable_from_name(const char *iconname, int target_size)
{
	GdkPaintable *paintable = NULL;
	GtkIconPaintable *icon;

	GtkIconTheme *theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
	icon = gtk_icon_theme_lookup_icon(theme,
	                                  iconname,
	                                  NULL,  // const char **fallbacks
	                                  target_size,
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
	// (v)

	if (err && g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_OBJECT)) {
		g_error_free(err);
		return;
	} else if (err) {
		g_warning("%s\n", err->message);
		g_error_free(err);
		return;
	}

	GVariant *iconname_v;
	const char *iconname = NULL;
	g_variant_get(data, "(v)", &iconname_v);
	g_variant_get(iconname_v, "&s", &iconname);
	g_variant_unref(iconname_v);

	if (strcmp(iconname, snitem->iconname) == 0) {
		// g_debug("%s got NewIcon, but iconname didn't change. Ignoring\n", snitem->busname);
		g_variant_unref(data);
		return;
	}

	g_free(snitem->iconname);
	g_object_unref(snitem->paintable);

	snitem->iconname = g_strdup(iconname);
	snitem->paintable = get_paintable_from_name(snitem->iconname, snitem->host->height);
	gtk_image_set_from_paintable(GTK_IMAGE(snitem->icon), snitem->paintable);

	g_variant_unref(data);
}


static void
new_iconpixmap_handler(GDBusProxy *proxy, GAsyncResult *res, StatusNotifierItem *snitem)
{
	GError *err = NULL;
	GVariant *data = g_dbus_proxy_call_finish(proxy, res, &err);
	// (v)

	if (err && g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_OBJECT)) {
		g_error_free(err);
		return;
	} else if (err) {
		g_warning("%s\n", err->message);
		g_error_free(err);
		return;
	}

	GVariant *newpixmap_v;
	g_variant_get(data, "(v)", &newpixmap_v);
	g_variant_unref(data);

	if (g_variant_equal(newpixmap_v, snitem->iconpixmap_v)) {
		// g_debug ("%s got NewIcon, but iconpixmap didn't change. Ignoring\n", snitem->busname);
		g_variant_unref(newpixmap_v);
		return;
	}

	g_object_unref(snitem->paintable);
	g_variant_unref(snitem->iconpixmap_v);
	snitem->iconpixmap_v = newpixmap_v;
	GdkPaintable *paintable = get_paintable_from_data(snitem->iconpixmap_v,
	                                                  snitem->host->height);
	gtk_image_set_from_paintable(GTK_IMAGE (snitem->icon), paintable);
	snitem->paintable = paintable;
}


static void
trayitem_signal_handler(GDBusProxy *proxy,
                        const char *sender,
                        const char *signal,
                        GVariant *data_v,
                        StatusNotifierItem *snitem)
{
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

	const char *iconname = NULL;
	GdkPaintable *paintable = NULL;
	GVariant *iconname_v = g_dbus_proxy_get_cached_property(proxy, "IconName");
	GVariant *iconpixmap_v = g_dbus_proxy_get_cached_property(proxy, "IconPixmap");

	if (iconname_v) {
		g_variant_get(iconname_v, "&s", &iconname);
		g_variant_unref(iconname_v);
	}

	if (iconname && strcmp(iconname, "") != 0) {
		paintable = get_paintable_from_name(iconname, snitem->host->height);

		snitem->iconname = g_strdup(iconname);

		if (iconpixmap_v)
			g_variant_unref(iconpixmap_v);

	} else if (iconpixmap_v) {
		paintable = get_paintable_from_data(iconpixmap_v, snitem->host->height);

		snitem->iconpixmap_v = iconpixmap_v;
	} else {
		paintable = get_paintable_from_name("missingicon", snitem->host->height);
	}

	image = gtk_image_new_from_paintable(paintable);
	snitem->paintable = paintable;

	return image;
}


void
create_trayitem(GObject *obj, GAsyncResult *res, StatusNotifierItem *snitem)
{
	GError *err = NULL;
	GDBusProxy *proxy = g_dbus_proxy_new_finish(res, &err);

	if (err) {
		g_error("%s\n", err->message);
		g_error_free(err);
	}

	// If this happens for whatever reason, we lose track of
	// our window size (there will be a gap between systray and bar)
	if (!snitem && proxy) {
		g_object_unref(proxy);
		return;
	} else if (!snitem) {
		return;
	}

	snitem->proxy = proxy;

	GVariant *iconthemepath_v;
	const char *iconthemepath;
	GtkIconTheme *theme;
	GtkGesture *leftclick;
	GtkGesture *rightclick;
	GVariant *menu_buspath_v;
	const char *menu_buspath;
	GSimpleActionGroup *actiongroup;
	GtkWidget *icon;

	g_signal_connect(proxy, "g-signal", G_CALLBACK(trayitem_signal_handler), snitem);

	iconthemepath_v = g_dbus_proxy_get_cached_property(proxy, "IconThemePath");
	theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
	if (iconthemepath_v) {
		g_variant_get(iconthemepath_v, "&s", &iconthemepath);
		gtk_icon_theme_add_search_path(theme, iconthemepath);
		g_variant_unref(iconthemepath_v);
	}

	icon = create_icon(proxy, snitem);
	snitem->icon = icon;

	leftclick = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(leftclick), 1);
	g_signal_connect(leftclick, "pressed", G_CALLBACK(on_leftclick_cb), snitem);

	rightclick = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rightclick), 3);
	g_signal_connect(rightclick, "pressed", G_CALLBACK(on_rightclick_cb), snitem);

	actiongroup = g_simple_action_group_new();
	snitem->actiongroup = actiongroup;

	gtk_widget_add_controller(icon, GTK_EVENT_CONTROLLER(leftclick));
	gtk_widget_add_controller(icon, GTK_EVENT_CONTROLLER(rightclick));
	gtk_widget_insert_action_group(icon,
				       "menuitem",
				       G_ACTION_GROUP(actiongroup));

	gtk_box_append(GTK_BOX(snitem->host->box), icon);

	menu_buspath_v = g_dbus_proxy_get_cached_property(proxy, "Menu");
	if (menu_buspath_v) {
		g_variant_get(menu_buspath_v, "&o", &menu_buspath);
		GDBusNodeInfo *nodeinfo = g_dbus_node_info_new_for_xml(DBUSMENU_XML, NULL);
		g_dbus_proxy_new_for_bus(G_BUS_TYPE_SESSION,
		                         G_DBUS_PROXY_FLAGS_NONE,
		                         nodeinfo->interfaces[0],
		                         snitem->busname,
		                         menu_buspath,
		                         "com.canonical.dbusmenu",
		                         NULL,
		                         (GAsyncReadyCallback)create_menu,
		                         snitem);
		g_dbus_node_info_unref(nodeinfo);
		g_variant_unref(menu_buspath_v);
	} else {
		g_bit_unlock(&snitem->lock, 0);
	}
}
