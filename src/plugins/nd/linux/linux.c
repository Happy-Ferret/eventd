/*
 * eventd - Small daemon to act on remote or local events
 *
 * Copyright © 2011-2012 Quentin "Sardem FF7" Glidic
 *
 * This file is part of eventd.
 *
 * eventd is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * eventd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with eventd. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/mman.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <cairo.h>

#include <eventd-nd-types.h>
#include <eventd-nd-style.h>
#include <eventd-nd-backend.h>
#include <eventd-nd-cairo.h>

#define FRAMEBUFFER_TARGET_PREFIX "/dev/tty"

struct _EventdNdBackendContext {
    EventdNdContext *nd;
    EventdNdInterface *nd_interface;
};

struct _EventdNdDisplay {
    gint fd;
    guchar *buffer;
    guint64 screensize;
    gint stride;
    gint channels;
    gint x;
    gint y;
};

struct _EventdNdSurface {
    cairo_surface_t *bubble;
    guchar *buffer;
    guchar *save;
    gint stride;
    gint channels;
};

static EventdNdBackendContext *
_eventd_nd_linux_init(EventdNdContext *nd, EventdNdInterface *nd_interface)
{
    EventdNdBackendContext *context;

    context = g_new0(EventdNdBackendContext, 1);

    context->nd = nd;
    context->nd_interface = nd_interface;

    eventd_nd_cairo_init();

    return context;
}

static void
_eventd_nd_linux_uninit(EventdNdBackendContext *context)
{
    eventd_nd_cairo_uninit();
    g_free(context);
}

static gboolean
_eventd_nd_linux_display_test(EventdNdBackendContext *context, const gchar *target)
{
    return g_str_has_prefix(target, FRAMEBUFFER_TARGET_PREFIX);
}

static EventdNdDisplay *
_eventd_nd_linux_display_new(EventdNdBackendContext *context, const gchar *target, EventdNdCornerAnchor anchor, gint margin)
{
    EventdNdDisplay *display;
    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo vinfo;

    display = g_new0(EventdNdDisplay, 1);

    display->fd = g_open("/dev/fb0", O_RDWR);
    if ( display->fd == -1 )
    {
        g_warning("Couldn't open framebuffer device: %s", g_strerror(errno));
        goto fail;
    }

    if ( ioctl(display->fd, FBIOGET_FSCREENINFO, &finfo) == -1 )
    {
        g_warning("Couldn't get framebuffer fixed info: %s", g_strerror(errno));
        goto fail;
    }

    if ( ioctl(display->fd, FBIOGET_VSCREENINFO, &vinfo) == -1 )
    {
        g_warning("Couldn't get framebuffer variable info: %s", g_strerror(errno));
        goto fail;
    }

    display->channels = vinfo.bits_per_pixel >> 3;
    display->stride = finfo.line_length;

    switch ( anchor )
    {
    case EVENTD_ND_ANCHOR_TOP_LEFT:
        display->x = margin;
        display->y = margin;
    break;
    case EVENTD_ND_ANCHOR_TOP_RIGHT:
        display->x = - vinfo.xres + margin;
        display->y = margin;
    break;
    case EVENTD_ND_ANCHOR_BOTTOM_LEFT:
        display->x = margin;
        display->y = - vinfo.yres + margin;
    break;
    case EVENTD_ND_ANCHOR_BOTTOM_RIGHT:
        display->x = - vinfo.xres + margin;
        display->y = - vinfo.yres + margin;
    break;
    }

    display->screensize = ( vinfo.xoffset * ( vinfo.yres - 1 ) + vinfo.xres * vinfo.yres ) * display->channels;

    display->buffer = mmap(NULL, display->screensize, PROT_READ|PROT_WRITE, MAP_SHARED, display->fd, ( vinfo.xoffset ) * display->channels + ( vinfo.yoffset ) * display->stride);
    if ( display->buffer == (void *)-1 )
    {
        g_warning("Couldn't map framebuffer device to memory: %s", strerror(errno));
        goto fail;
    }


    return display;

fail:
    g_free(display);
    return NULL;
}

static void
_eventd_nd_linux_display_free(EventdNdDisplay *display)
{
    munmap(display->buffer, display->screensize);
    close(display->fd);

    g_free(display);
}

static inline guchar
alpha_div(guchar c, guchar a)
{
    guint16 t;
    return c;
    switch ( a )
    {
    case 0xff:
        return c;
    case 0x00:
        return 0x00;
    default:
        t = c / a + 0x7f;
        return ((t << 8) + t) << 8;
    }
}

static EventdNdSurface *
_eventd_nd_linux_surface_show(EventdEvent *event, EventdNdDisplay *display, EventdNdNotification *notification, EventdNdStyle *style)
{
    EventdNdSurface *self;
    gint x, y;

    cairo_surface_t *bubble;
    cairo_surface_t *shape;

    gint width;
    gint height;

    eventd_nd_cairo_get_surfaces(event, notification, style, &bubble, &shape);

    width = cairo_image_surface_get_width(bubble);
    height = cairo_image_surface_get_height(bubble);

    x = display->x;
    y = display->y;

    if ( x < 0 )
        x = - x - width;
    if ( y < 0 )
        y = - y - height;

    self = g_new0(EventdNdSurface, 1);

    self->bubble = cairo_surface_reference(bubble);
    self->buffer = display->buffer + x * display->channels + y * display->stride;
    self->stride = display->stride;
    self->channels = display->channels;

    self->save = g_malloc(self->channels * cairo_image_surface_get_height(self->bubble) * cairo_image_surface_get_width(self->bubble));

    guchar *spixels, *sline;
    gint sstride;
    guchar *pixels, *line;
    const guchar *cpixels, *cpixels_end, *cline, *cline_end;
    gint cstride, clo;
    gint w;

    w = cairo_image_surface_get_width(self->bubble);

    cpixels = cairo_image_surface_get_data(self->bubble);
    cstride = cairo_image_surface_get_stride(self->bubble);
    cpixels_end = cpixels + cstride * cairo_image_surface_get_height(self->bubble);
    clo =  w << 2;

    spixels = self->save;
    sstride = w * self->channels;

    pixels = self->buffer;

    while ( cpixels < cpixels_end )
    {
        sline = spixels;
        line = pixels;
        cline = cpixels;
        cline_end = cline + clo;

        while ( cline < cline_end )
        {
            sline[0] = line[0];
            sline[1] = line[1];
            sline[2] = line[2];
            sline[3] = line[3];

            line[0] = alpha_div(cline[0], cline[3]);
            line[1] = alpha_div(cline[1], cline[3]);
            line[2] = alpha_div(cline[2], cline[3]);
            line[3] = ~cline[3];

            sline += self->channels;
            line += self->channels;
            cline += 4;
        }

        spixels += sstride;
        pixels += self->stride;
        cpixels += cstride;
    }

    return self;
}

static void
_eventd_nd_linux_surface_hide(EventdNdSurface *self)
{
    guchar *pixels, *line;
    const guchar *spixels, *spixels_end, *sline, *sline_end;
    gint sstride, slo;

    spixels = self->save;
    sstride = slo = self->channels * cairo_image_surface_get_width(self->bubble);
    spixels_end = self->save + sstride * cairo_image_surface_get_height(self->bubble);

    pixels = self->buffer;

    while ( spixels < spixels_end )
    {
        sline = spixels;
        sline_end = sline + slo;
        line = pixels;

        while ( sline < sline_end )
        {
            line[0] = sline[0];
            line[1] = sline[1];
            line[2] = sline[2];
            line[3] = sline[3];

            sline += self->channels;
            line += self->channels;
        }

        pixels += self->stride;
        spixels += sstride;
    }

    cairo_surface_destroy(self->bubble);

    g_free(self->save);

    g_free(self);
}

void
eventd_nd_backend_get_info(EventdNdBackend *backend)
{
    backend->init = _eventd_nd_linux_init;
    backend->uninit = _eventd_nd_linux_uninit;

    backend->display_test = _eventd_nd_linux_display_test;
    backend->display_new = _eventd_nd_linux_display_new;
    backend->display_free = _eventd_nd_linux_display_free;

    backend->surface_show = _eventd_nd_linux_surface_show;
    backend->surface_hide = _eventd_nd_linux_surface_hide;
}
