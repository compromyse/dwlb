#ifndef SNHOST_H
#define SNHOST_H

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SN_TYPE_HOST sn_host_get_type()
G_DECLARE_FINAL_TYPE(SnHost, sn_host, SN, HOST, GtkBox)

SnHost	*sn_host_new		(const char *traymon,
				int iconsize,
				int margins,
				int spacing);

G_END_DECLS

#define STATUSNOTIFIERWATCHER_XML	\
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"	\
	"<node>\n"	\
	"    <interface name=\"org.kde.StatusNotifierWatcher\">\n"	\
	"        <!-- methods -->\n"	\
	"        <method name=\"RegisterStatusNotifierItem\">\n"	\
	"            <arg name=\"service\" type=\"s\" direction=\"in\" />\n"	\
	"        </method>\n"	\
	"        <!-- properties -->\n"	\
	"        <property name=\"IsStatusNotifierHostRegistered\" type=\"b\" access=\"read\" />\n"	\
	"        <property name=\"ProtocolVersion\" type=\"i\" access=\"read\" />\n"	\
	"        <property name=\"RegisteredStatusNotifierItems\" type=\"as\" access=\"read\" />\n"	\
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

#endif /* SNHOST_H */
