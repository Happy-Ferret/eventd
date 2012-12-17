/*
 * libeventd-evp - Library to send EVENT protocol messages
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#include <glib.h>
#include <gio/gio.h>

#include <libeventd-event.h>
#include <libeventd-event-private.h>

#include <libeventd-evp.h>

#include "context.h"

static void
_libeventd_evp_context_send_hello_check_callback(LibeventdEvpContext *self, const gchar *line)
{
    g_return_if_fail(self != NULL);

    GSimpleAsyncResult *result = self->waiter.result;

    if ( g_strcmp0(line, "HELLO") != 0 )
        g_simple_async_result_set_error(result, LIBEVENTD_EVP_ERROR, LIBEVENTD_EVP_ERROR_HELLO, "Wrong HELLO aknowledgement message: %s", line);

    g_simple_async_result_complete_in_idle(result);
    g_object_unref(result);
}

void
libeventd_evp_context_send_hello(LibeventdEvpContext *self, const gchar *category, GAsyncReadyCallback callback, gpointer user_data)
{
    g_return_if_fail(self != NULL);
    g_return_if_fail(category != NULL);

    gboolean r;
    gchar *message;
    GError *error = NULL;

    message = g_strdup_printf("HELLO %s", category);
    r = libeventd_evp_context_send_message(self, message, &error);
    g_free(message);

    if ( ! r )
        g_simple_async_report_take_gerror_in_idle(G_OBJECT(self->cancellable), callback, user_data, error);
    else
    {
        self->waiter.callback = _libeventd_evp_context_send_hello_check_callback;
        self->waiter.result = g_simple_async_result_new(G_OBJECT(self->cancellable), callback, user_data, libeventd_evp_context_send_hello);
    }
}

gboolean
libeventd_evp_context_send_hello_finish(LibeventdEvpContext *self, GAsyncResult *result, GError **error)
{
    g_return_val_if_fail(self != NULL, FALSE);
    g_return_val_if_fail(g_simple_async_result_is_valid(result, G_OBJECT(self->cancellable), NULL), FALSE);

    GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT(result);

    if ( g_simple_async_result_propagate_error(simple, error) )
        return FALSE;

    return TRUE;
}


static gboolean
_libeventd_evp_context_send_data(LibeventdEvpContext *self, GHashTable *all_data, GError **error)
{
    if ( all_data == NULL )
        return TRUE;

    gchar *message;

    GHashTableIter iter;
    const gchar *name;
    const gchar *content;

    g_hash_table_iter_init(&iter, all_data);
    while ( g_hash_table_iter_next(&iter, (gpointer *)&name, (gpointer *)&content) )
    {
        if ( g_utf8_strchr(content, -1, '\n') == NULL )
        {
            message = g_strdup_printf("DATAL %s %s", name, content);
            if ( ! libeventd_evp_context_send_message(self, message, error) )
                goto fail;
            g_free(message);
        }
        else
        {
            message = g_strdup_printf("DATA %s", name);
            if ( ! libeventd_evp_context_send_message(self, message, error) )
                goto fail;
            g_free(message);

            gchar **data;
            data = g_strsplit(content, "\n", -1);

            gchar **line;
            for ( line = data ; *line != NULL ; ++line )
            {
                if ( (*line)[0] == '.' )
                {
                    GError *_inner_error_ = NULL;
                    if ( ! g_data_output_stream_put_byte(self->out, '.', self->cancellable, &_inner_error_) )
                    {
                        g_propagate_error(error, _inner_error_);
                        g_strfreev(data);
                        return FALSE;
                    }
                }
                if ( ! libeventd_evp_context_send_message(self, *line, error) )
                {
                    g_strfreev(data);
                    return FALSE;
                }
            }

            g_strfreev(data);
            if ( ! libeventd_evp_context_send_message(self, ".", error) )
                return FALSE;
        }
    }

    return TRUE;

fail:
    g_free(message);
    return FALSE;
}

static void
_libeventd_evp_context_send_event_check_callback(LibeventdEvpContext *self, const gchar *line)
{
    g_return_if_fail(self != NULL);

    GSimpleAsyncResult *result = self->waiter.result;

    if ( ! g_str_has_prefix(line, "EVENT ") )
        g_simple_async_result_set_error(result, LIBEVENTD_EVP_ERROR, LIBEVENTD_EVP_ERROR_EVENT, "Wrong EVENT aknowledgement message: %s", line);
    else
        g_simple_async_result_set_op_res_gpointer(result, g_strdup(line + strlen("EVENT ")), g_free);

    g_simple_async_result_complete_in_idle(result);
    g_object_unref(result);
}

void
libeventd_evp_context_send_event(LibeventdEvpContext *self, EventdEvent *event, GAsyncReadyCallback callback, gpointer user_data)
{
    g_return_if_fail(self != NULL);
    g_return_if_fail(event != NULL);

    gchar *message;
    GError *error = NULL;

    message = g_strdup_printf("EVENT %s", eventd_event_get_name(event));
    if ( ! libeventd_evp_context_send_message(self, message, &error) )
        goto fail;
    g_free(message);

    const gchar *category;
    category = eventd_event_get_category(event);
    if ( category != NULL )
    {
        message = g_strdup_printf("CATEGORY %s", category);
        if ( ! libeventd_evp_context_send_message(self, message, &error) )
            goto fail;
        g_free(message);
    }

    GList *answer_;
    for ( answer_ = eventd_event_get_answers(event) ; answer_ != NULL ; answer_ = g_list_next(answer_) )
    {
        const gchar *answer = answer_->data;
        message = g_strdup_printf("ANSWER %s", answer);
        if ( ! libeventd_evp_context_send_message(self, message, &error) )
            goto fail;
        g_free(message);
    }

    message = NULL;

    if ( ! _libeventd_evp_context_send_data(self, eventd_event_get_all_data(event), &error) )
        goto fail;

    if ( ! libeventd_evp_context_send_message(self, ".", &error) )
        goto fail;

    self->waiter.callback = _libeventd_evp_context_send_event_check_callback;
    self->waiter.result = g_simple_async_result_new(G_OBJECT(self->cancellable), callback, user_data, libeventd_evp_context_send_event);

    return;

fail:
    g_free(message);
    g_simple_async_report_take_gerror_in_idle(G_OBJECT(self->cancellable), callback, user_data, error);
}

const gchar *
libeventd_evp_context_send_event_finish(LibeventdEvpContext *self, GAsyncResult *result, GError **error)
{
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(g_simple_async_result_is_valid(result, G_OBJECT(self->cancellable), NULL), NULL);

    GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT(result);

    if ( g_simple_async_result_propagate_error(simple, error) )
        return NULL;

    return g_simple_async_result_get_op_res_gpointer(simple);
}

static void
_libeventd_evp_context_send_end_check_callback(LibeventdEvpContext *self, const gchar *line)
{
    g_return_if_fail(self != NULL);

    GSimpleAsyncResult *result = self->waiter.result;
    gchar *id = self->waiter.id;

    if ( ! g_str_has_prefix(line, "ENDING ") )
        g_simple_async_result_set_error(result, LIBEVENTD_EVP_ERROR, LIBEVENTD_EVP_ERROR_END, "Wrong END aknowledgement message: %s", line);
    else if ( g_strcmp0(line + strlen("ENDING "), id) != 0 )
        g_simple_async_result_set_error(result, LIBEVENTD_EVP_ERROR, LIBEVENTD_EVP_ERROR_END, "Wrong event id for END message (wanted '%s'): %s", id, line + strlen("ENDING "));

    g_free(id);

    g_simple_async_result_complete_in_idle(result);
    g_object_unref(result);
}

void
libeventd_evp_context_send_end(LibeventdEvpContext *self, const gchar *id, GAsyncReadyCallback callback, gpointer user_data)
{
    g_return_if_fail(self != NULL);
    g_return_if_fail(id != NULL);

    gboolean r;
    gchar *message;
    GError *error = NULL;

    message = g_strdup_printf("END %s", id);
    r = libeventd_evp_context_send_message(self, message, &error);
    g_free(message);

    if ( ! r )
        g_simple_async_report_take_gerror_in_idle(G_OBJECT(self->cancellable), callback, user_data, error);
    else
    {
        self->waiter.callback = _libeventd_evp_context_send_end_check_callback;
        self->waiter.result = g_simple_async_result_new(G_OBJECT(self->cancellable), callback, user_data, libeventd_evp_context_send_end);
        self->waiter.id = g_strdup(id);
    }
}

gboolean
libeventd_evp_context_send_end_finish(LibeventdEvpContext *self, GAsyncResult *result, GError **error)
{
    g_return_val_if_fail(self != NULL, FALSE);
    g_return_val_if_fail(g_simple_async_result_is_valid(result, G_OBJECT(self->cancellable), NULL), FALSE);

    GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT(result);

    if ( g_simple_async_result_propagate_error(simple, error) )
        return FALSE;

    return TRUE;
}

gboolean
libeventd_evp_context_send_answered(LibeventdEvpContext *self, const gchar *id, const gchar *answer, GHashTable *data, GError **error)
{
    g_return_val_if_fail(self != NULL, FALSE);
    g_return_val_if_fail(id != NULL, FALSE);
    g_return_val_if_fail(answer != NULL, FALSE);

    gchar *message;

    message = g_strdup_printf("ANSWERED %s %s", id, answer);
    if ( ! libeventd_evp_context_send_message(self, message, error) )
        goto fail;
    g_free(message);

    if ( ! _libeventd_evp_context_send_data(self, data, error) )
        return FALSE;

    if ( ! libeventd_evp_context_send_message(self, ".", error) )
        return FALSE;

    return TRUE;

fail:
    g_free(message);
    return FALSE;
}

gboolean
libeventd_evp_context_send_ended(LibeventdEvpContext *self, const gchar *id, EventdEventEndReason reason, GError **error)
{
    g_return_val_if_fail(self != NULL, FALSE);
    g_return_val_if_fail(id != NULL, FALSE);

    const gchar *reason_text = "";
    switch ( reason )
    {
    case EVENTD_EVENT_END_REASON_NONE:
        reason_text = "none";
    break;
    case EVENTD_EVENT_END_REASON_TIMEOUT:
        reason_text = "timeout";
    break;
    case EVENTD_EVENT_END_REASON_USER_DISMISS:
        reason_text = "user-dismiss";
    break;
    case EVENTD_EVENT_END_REASON_CLIENT_DISMISS:
        reason_text = "client-dismiss";
    break;
    case EVENTD_EVENT_END_REASON_RESERVED:
        reason_text = "reserved";
    break;
    }

    gboolean r;
    gchar *message;

    message = g_strdup_printf("ENDED %s %s", id, reason_text);
    r = libeventd_evp_context_send_message(self, message, error);
    g_free(message);

    return r;
}


void
libeventd_evp_context_send_bye(LibeventdEvpContext *self)
{
    g_return_if_fail(self != NULL);

    libeventd_evp_context_send_message(self, "BYE", NULL);
}


gboolean
libeventd_evp_context_send_message(LibeventdEvpContext *self, const gchar *message, GError **error)
{
    GError *_inner_error_ = NULL;

    g_return_val_if_fail(self != NULL, FALSE);
    g_return_val_if_fail(message != NULL, FALSE);

    if ( ! g_data_output_stream_put_string(self->out, message, self->cancellable, &_inner_error_) )
    {
        g_propagate_error(error, _inner_error_);
        return FALSE;
    }
    if ( ! g_data_output_stream_put_byte(self->out, '\n', self->cancellable, &_inner_error_) )
    {
        g_propagate_error(error, _inner_error_);
        return FALSE;
    }

    return TRUE;
}
