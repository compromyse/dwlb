#ifndef GTKTRAY_H
#define GTKTRAY_H

#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

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
	char *cssdata;
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


#define DBUSMENU_XML	\
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"	\
	"<node>\n"	\
	"    <interface name=\"com.canonical.dbusmenu\">\n"	\
	"        <!-- methods -->\n"	\
	"        <method name=\"GetLayout\">\n"	\
	"            <arg type=\"i\" name=\"parentId\" direction=\"in\"/>\n"	\
	"            <arg type=\"i\" name=\"recursionDepth\" direction=\"in\"/>\n"	\
	"            <arg type=\"as\" name=\"propertyNames\" direction=\"in\"/>\n"	\
	"            <arg type=\"u\" name=\"revision\" direction=\"out\"/>\n"	\
	"            <arg type=\"(ia{sv}av)\" name=\"layout\" direction=\"out\"/>\n"	\
	"        </method>\n"	\
	"        <method name=\"Event\">\n"	\
	"            <arg type=\"i\" name=\"id\" direction=\"in\"/>\n"	\
	"            <arg type=\"s\" name=\"eventId\" direction=\"in\"/>\n"	\
	"            <arg type=\"v\" name=\"data\" direction=\"in\"/>\n"	\
	"            <arg type=\"u\" name=\"timestamp\" direction=\"in\"/>\n"	\
	"        </method>\n"	\
	"        <method name=\"AboutToShow\">\n"	\
	"            <arg type=\"i\" name=\"id\" direction=\"in\"/>\n"	\
	"            <arg type=\"b\" name=\"needUpdate\" direction=\"out\"/>\n"	\
	"        </method>\n"	\
	"        <!--\n"	\
	"        <method name=\"AboutToShowGroup\">\n"	\
	"            <arg type=\"ai\" name=\"ids\" direction=\"in\"/>\n"	\
	"            <arg type=\"ai\" name=\"updatesNeeded\" direction=\"out\"/>\n"	\
	"            <arg type=\"ai\" name=\"idErrors\" direction=\"out\"/>\n"	\
	"        </method>\n"	\
	"        <method name=\"GetGroupProperties\">\n"	\
	"            <arg type=\"ai\" name=\"ids\" direction=\"in\"/>\n"	\
	"            <arg type=\"as\" name=\"propertyNames\" direction=\"in\"/>\n"	\
	"            <arg type=\"a(ia{sv})\" name=\"properties\" direction=\"out\"/>\n"	\
	"        </method>\n"	\
	"        <method name=\"GetProperty\">\n"	\
	"            <arg type=\"i\" name=\"id\" direction=\"in\"/>\n"	\
	"            <arg type=\"s\" name=\"name\" direction=\"in\"/>\n"	\
	"            <arg type=\"v\" name=\"value\" direction=\"out\"/>\n"	\
	"        </method>\n"	\
	"        <method name=\"EventGroup\">\n"	\
	"            <arg type=\"a(isvu)\" name=\"events\" direction=\"in\"/>\n"	\
	"            <arg type=\"ai\" name=\"idErrors\" direction=\"out\"/>\n"	\
	"        </method>\n"	\
	"        -->\n"	\
	"        <!-- properties -->\n"	\
	"        <!--\n"	\
	"        <property name=\"Version\" type=\"u\" access=\"read\"/>\n"	\
	"        <property name=\"TextDirection\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"Status\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"IconThemePath\" type=\"as\" access=\"read\"/>\n"	\
	"        -->\n"	\
	"        <!-- Signals -->\n"	\
	"        <signal name=\"ItemsPropertiesUpdated\">\n"	\
	"            <arg type=\"a(ia{sv})\" name=\"updatedProps\" direction=\"out\"/>\n"	\
	"            <arg type=\"a(ias)\" name=\"removedProps\" direction=\"out\"/>\n"	\
	"        </signal>\n"	\
	"        <signal name=\"LayoutUpdated\">\n"	\
	"            <arg type=\"u\" name=\"revision\" direction=\"out\"/>\n"	\
	"            <arg type=\"i\" name=\"parent\" direction=\"out\"/>\n"	\
	"        </signal>\n"	\
	"        <!--\n"	\
	"        <signal name=\"ItemActivationRequested\">\n"	\
	"            <arg type=\"i\" name=\"id\" direction=\"out\"/>\n"	\
	"            <arg type=\"u\" name=\"timestamp\" direction=\"out\"/>\n"	\
	"        </signal>\n"	\
	"        -->\n"	\
	"    </interface>\n"	\
	"</node>\n"


#define STATUSNOTIFIERITEM_XML	\
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"	\
	"<node>\n"	\
	"    <interface name=\"org.kde.StatusNotifierItem\">\n"	\
	"        <!-- methods -->\n"	\
	"        <method name=\"Activate\">\n"	\
	"            <arg name=\"x\" type=\"i\" direction=\"in\"/>\n"	\
	"            <arg name=\"y\" type=\"i\" direction=\"in\"/>\n"	\
	"        </method>\n"	\
	"        <!--\n"	\
	"        <method name=\"Scroll\">\n"	\
	"          <arg name=\"delta\" type=\"i\" direction=\"in\"/>\n"	\
	"          <arg name=\"orientation\" type=\"s\" direction=\"in\"/>\n"	\
	"        </method>\n"	\
	"        <method name=\"ContextMenu\">\n"	\
	"            <arg name=\"x\" type=\"i\" direction=\"in\"/>\n"	\
	"            <arg name=\"y\" type=\"i\" direction=\"in\"/>\n"	\
	"        </method>\n"	\
	"        <method name=\"SecondaryActivate\">\n"	\
	"            <arg name=\"x\" type=\"i\" direction=\"in\"/>\n"	\
	"            <arg name=\"y\" type=\"i\" direction=\"in\"/>\n"	\
	"        </method>\n"	\
	"        -->\n"	\
	"        <!-- properties -->\n"	\
	"        <property name=\"Menu\" type=\"o\" access=\"read\"/>\n"	\
	"        <property name=\"IconName\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"IconPixmap\" type=\"a(iiay)\" access=\"read\"/>\n"	\
	"        <property name=\"IconThemePath\" type=\"s\" access=\"read\"/>\n"	\
	"        <!--\n"	\
	"        <property name=\"OverlayIconName\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"OverlayIconPixmap\" type=\"a(iiay)\" access=\"read\"/>\n"	\
	"        <property name=\"AttentionIconName\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"AttentionIconPixmap\" type=\"a(iiay)\" access=\"read\"/>\n"	\
	"        <property name=\"Category\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"Id\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"Title\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"Status\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"WindowId\" type=\"i\" access=\"read\"/>\n"	\
	"        <property name=\"ItemIsMenu\" type=\"b\" access=\"read\"/>\n"	\
	"        <property name=\"AttentionMovieName\" type=\"s\" access=\"read\"/>\n"	\
	"        <property name=\"ToolTip\" type=\"(sa(iiay)ss)\" access=\"read\"/>\n"	\
	"        -->\n"	\
	"        <!-- signals -->\n"	\
	"        <signal name=\"NewIcon\"/>\n"	\
	"        <!--\n"	\
	"        <signal name=\"NewAttentionIcon\"/>\n"	\
	"        <signal name=\"NewOverlayIcon\"/>\n"	\
	"        <signal name=\"NewTitle\"/>\n"	\
	"        <signal name=\"NewToolTip\"/>\n"	\
	"        <signal name=\"NewStatus\">\n"	\
	"          <arg name=\"status\" type=\"s\"/>\n"	\
	"        </signal>\n"	\
	"        -->\n"	\
	"  </interface>\n"	\
	"</node>\n"


#define STATUSNOTIFIERWATCHER_XML	\
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"	\
	"<node>\n"	\
	"    <interface name=\"org.kde.StatusNotifierWatcher\">\n"	\
	"        <!-- methods -->\n"	\
	"        <method name=\"RegisterStatusNotifierItem\">\n"	\
	"            <arg name=\"service\" type=\"s\" direction=\"in\" />\n"	\
	"        </method>\n"	\
	"        <!-- properties -->\n"	\
	"        <property name=\"RegisteredStatusNotifierItems\" type=\"as\" access=\"read\" />\n"	\
	"        <property name=\"IsStatusNotifierHostRegistered\" type=\"b\" access=\"read\" />\n"	\
	"        <property name=\"ProtocolVersion\" type=\"i\" access=\"read\" />\n"	\
	"        <!-- signals -->\n"	\
	"        <signal name=\"StatusNotifierItemRegistered\">\n"	\
	"            <arg type=\"s\"/>\n"	\
	"        </signal>\n"	\
	"        <signal name=\"StatusNotifierItemUnregistered\">\n"	\
	"            <arg type=\"s\"/>\n"	\
	"        </signal>\n"	\
	"        <signal name=\"StatusNotifierHostRegistered\">\n"	\
	"        </signal>\n"	\
	"    </interface>\n"	\
	"</node>\n"

#endif /* STATUSNOTIFIERHOST_H */
