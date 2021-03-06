# Basic CLI client
bin_PROGRAMS += \
	eventc \
	$(null)

eventc_SOURCES = \
	%D%/src/eventc.c \
	$(null)

eventc_CFLAGS = \
	$(AM_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GOBJECT_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(null)

eventc_LDADD = \
	libeventd.la \
	libeventc.la \
	$(GIO_LIBS) \
	$(GOBJECT_LIBS) \
	$(GLIB_LIBS) \
	$(null)
