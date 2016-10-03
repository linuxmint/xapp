CLEANFILES =

noinst_LTLIBRARIES =
lib_LTLIBRARIES = libxapp.la

AM_CPPFLAGS =							\
	-I$(top_srcdir)						\
	$(XAPP_CFLAGS)					\
	$(XLIB_CFLAGS)						\
	-DG_LOG_DOMAIN=\"XApp\"				\
	-DGNOMELOCALEDIR=\""$(prefix)/$(DATADIRNAME)/locale\""	\
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

introspection_sources = 		\
	xapp-display.c \
    xapp-kbd-layout-controller.c

libxapp_la_SOURCES = 	\
	$(introspection_sources)

libxapp_la_LIBADD =	\
	$(XLIB_LIBS)		\
	$(XAPP_LIBS)	\
	-lrt

libxapp_la_LDFLAGS = \
	-version-info $(LT_VERSION) \
	-export-symbols-regex "^xapp_.*" \
	-no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = xapp.pc

libxappdir = $(includedir)/xapp/libxapp
libxapp_HEADERS = \
	xapp-display.h \
    xapp-kbd-layout-controller.h

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir) --warn-all
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

if HAVE_INTROSPECTION

XApp-1.0.gir: libxapp.la
XApp_1_0_gir_INCLUDES = GObject-2.0 Gtk-3.0 Clutter-1.0
XApp_1_0_gir_PACKAGES = gdk-pixbuf-2.0 glib-2.0 gobject-2.0 gio-2.0 gtk+-3.0 clutter-1.0
XApp_1_0_gir_EXPORT_PACKAGES = xapp
XApp_1_0_gir_CFLAGS = -DGNOME_DESKTOP_USE_UNSTABLE_API -I$(top_srcdir)
XApp_1_0_gir_LIBS = libxapp.la
XApp_1_0_gir_FILES = $(introspection_sources) $(libxapp_HEADERS)
XApp_1_0_gir_SCANNERFLAGS = --identifier-prefix=XApp --symbol-prefix=xapp_
INTROSPECTION_SCANNER_ARGS += $(patsubst %,--c-include='libxapp/%',$(libxapp_HEADERS))
INTROSPECTION_GIRS += XApp-1.0.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)
endif

EXTRA_DIST = \
	xapp.pc.in			\
	xapp-uninstalled.pc.in	\
	$(pnpdata_DATA_dist)

-include $(top_srcdir)/git.mk
