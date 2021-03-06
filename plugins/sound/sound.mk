# sound plugin

if ENABLE_SOUND
plugins_LTLIBRARIES += \
	sound.la \
	$(null)

man5_MANS += \
	%D%/man/eventd-sound.conf.5 \
	$(null)
endif


sound_la_SOURCES = \
	%D%/src/pulseaudio.h \
	%D%/src/pulseaudio.c \
	%D%/src/sound.c \
	$(null)

sound_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(SNDFILE_CFLAGS) \
	$(PULSEAUDIO_CFLAGS) \
	$(GOBJECT_CFLAGS) \
	$(NKUTILS_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(null)

sound_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	$(PLUGIN_LDFLAGS) \
	$(null)

sound_la_LIBADD = \
	libeventd.la \
	libeventd-plugin.la \
	libeventd-helpers.la \
	$(SNDFILE_LIBS) \
	$(PULSEAUDIO_LIBS) \
	$(GOBJECT_LIBS) \
	$(NKUTILS_LIBS) \
	$(GLIB_LIBS) \
	$(null)
