# Main eventd library

LIBEVENTD_CURRENT=0
LIBEVENTD_REVISION=0
LIBEVENTD_AGE=0


AM_CFLAGS += \
	-I $(srcdir)/%D%/include \
	$(null)


lib_LTLIBRARIES += \
	libeventd.la \
	$(null)

pkginclude_HEADERS += \
	%D%/include/libeventd-event.h \
	%D%/include/libeventd-protocol.h \
	$(null)

TESTS += \
	libeventd-event.test \
	libeventd-protocol.test \
	$(null)

check_PROGRAMS += \
	libeventd-event.test \
	libeventd-protocol.test \
	$(null)

pkgconfig_DATA += \
	%D%/pkgconfig/libeventd.pc \
	$(null)

dist_aclocal_DATA += \
	%D%/aclocal/eventd.m4 \
	$(null)

dist_vapi_DATA += \
	%D%/vapi/libeventd.vapi \
	$(null)

vapi_DATA += \
	%D%/vapi/libeventd.deps \
	$(null)


libeventd_la_SOURCES = \
	%D%/include/libeventd-event-private.h \
	%D%/src/event-private.h \
	%D%/src/event-private.c \
	%D%/src/event.c \
	%D%/src/protocol-evp-private.h \
	%D%/src/protocol-evp.c \
	%D%/src/protocol-evp-parser.c \
	%D%/src/protocol-evp-generator.c \
	$(null)

libeventd_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(NKUTILS_CFLAGS) \
	$(GOBJECT_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(null)

libeventd_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	-version-info $(LIBEVENTD_CURRENT):$(LIBEVENTD_REVISION):$(LIBEVENTD_AGE) \
	$(null)

libeventd_la_LIBADD = \
	$(NKUTILS_LIBS) \
	$(GOBJECT_LIBS) \
	$(GLIB_LIBS) \
	$(null)



libeventd_event_test_SOURCES = \
	%D%/src/event-private.c \
	%D%/tests/unit/common.h \
	%D%/tests/unit/event-getters.c \
	%D%/tests/unit/event-getters.h \
	%D%/tests/unit/event-setters.c \
	%D%/tests/unit/event-setters.h \
	%D%/tests/unit/libeventd-event.c \
	$(null)

libeventd_event_test_CFLAGS = \
	$(AM_CFLAGS) \
	$(NKUTILS_CFLAGS) \
	$(GOBJECT_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(null)

libeventd_event_test_LDADD = \
	libeventd.la \
	$(NKUTILS_LIBS) \
	$(GOBJECT_LIBS) \
	$(GLIB_LIBS) \
	$(null)

libeventd_protocol_test_SOURCES = \
	%D%/src/event-private.c \
	%D%/tests/unit/common.h \
	%D%/tests/unit/protocol-parser.c \
	%D%/tests/unit/protocol-parser.h \
	%D%/tests/unit/protocol-generator.c \
	%D%/tests/unit/protocol-generator.h \
	%D%/tests/unit/libeventd-protocol.c \
	$(null)

libeventd_protocol_test_CFLAGS = \
	$(AM_CFLAGS) \
	$(NKUTILS_CFLAGS) \
	$(GOBJECT_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(null)

libeventd_protocol_test_LDADD = \
	libeventd.la \
	$(NKUTILS_LIBS) \
	$(GOBJECT_LIBS) \
	$(GLIB_LIBS) \
	$(null)


Eventd-0.gir: libeventd.la
Eventd_0_gir_INCLUDES = GObject-2.0
Eventd_0_gir_CFLAGS = $(AM_CFLAGS) $(libeventd_la_CFLAGS) $(CFLAGS)
Eventd_0_gir_LIBS = libeventd.la
Eventd_0_gir_FILES = $(filter-out %.h,$(libeventd_la_SOURCES)) $(filter %D%/include/%.h,$(pkginclude_HEADERS))
INTROSPECTION_GIRS += Eventd-0.gir
