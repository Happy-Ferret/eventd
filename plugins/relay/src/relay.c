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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <libeventd-event.h>
#include <eventd-plugin.h>
#include <libeventd-config.h>


#include "server.h"
#include "avahi.h"

struct _EventdPluginContext {
    EventdRelayAvahi *avahi;
    GHashTable *servers;
    GHashTable *events;
};


/*
 * Initialization interface
 */

static EventdPluginContext *
_eventd_relay_init(EventdCoreContext *core, EventdCoreInterface *interface)
{
    EventdPluginContext *context;

    context = g_new0(EventdPluginContext, 1);

    context->events = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_list_free);

#ifdef ENABLE_AVAHI
    context->avahi = eventd_relay_avahi_init();
#endif /* ENABLE_AVAHI */
    context->servers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, eventd_relay_server_free);

    return context;
}

static void
_eventd_relay_uninit(EventdPluginContext *context)
{
    g_hash_table_unref(context->servers);
#ifdef ENABLE_AVAHI
    eventd_relay_avahi_uninit(context->avahi);
#endif /* ENABLE_AVAHI */

    g_hash_table_unref(context->events);

    g_free(context);
}


/*
 * Start/Stop interface
 */

static void
_eventd_relay_start_each(gpointer key, gpointer data, gpointer user_data)
{
    if ( data == NULL )
        return;

    eventd_relay_server_start(data);
}

static void
_eventd_relay_start(EventdPluginContext *context)
{
    g_hash_table_foreach(context->servers, _eventd_relay_start_each, NULL);
#ifdef ENABLE_AVAHI
    eventd_relay_avahi_start(context->avahi);
#endif /* ENABLE_AVAHI */
}

static void
_eventd_relay_stop_each(gpointer key, gpointer data, gpointer user_data)
{
    if ( data == NULL )
        return;

    eventd_relay_server_stop(data);
}

static void
_eventd_relay_stop(EventdPluginContext *context)
{
#ifdef ENABLE_AVAHI
    eventd_relay_avahi_stop(context->avahi);
#endif /* ENABLE_AVAHI */
    g_hash_table_foreach(context->servers, _eventd_relay_stop_each, NULL);
}


/*
 * Control command interface
 */

static char *
_eventd_relay_control_command(EventdPluginContext *context, const gchar *command)
{
    const gchar *name;
    EventdRelayServer *server;
    gchar *message;

    if ( g_str_has_prefix(command, "connect ") )
    {
        name = command + strlen("connect ");
        server = g_hash_table_lookup(context->servers, name);
        if ( server == NULL )
            message = g_strdup_printf("No such server: %s", name);
        else
        {
            eventd_relay_server_start(server);
            message = g_strdup_printf("Connected to server '%s'", name);
        }
    }
    else if ( g_str_has_prefix(command, "disconnect ") )
    {
        name = command + strlen("disconnect ");
        server = g_hash_table_lookup(context->servers, name);
        if ( server == NULL )
            message = g_strdup_printf("No such server: %s", name);
        else
        {
            eventd_relay_server_stop(server);
            message = g_strdup_printf("Discennected from server '%s'", name);
        }
    }
    else
        message = g_strdup("Unknown command");

    return message;
}


/*
 * Configuration interface
 */

static void
_eventd_relay_event_parse(EventdPluginContext *context, const gchar *id, GKeyFile *config_file)
{
    gboolean disable;

    if ( ! g_key_file_has_group(config_file, "Relay") )
        return;

    if ( libeventd_config_key_file_get_boolean(config_file, "Relay", "Disable", &disable) < 0 )
        return;
    if ( disable )
    {
        g_hash_table_insert(context->events, g_strdup(id), NULL);
        return;
    }

    GList *list = NULL;

    gchar **server_uris;
    if ( libeventd_config_key_file_get_string_list(config_file, "Relay", "Servers", &server_uris, NULL) == 0 )
    {
        gchar **server_uri;
        for ( server_uri = server_uris ; *server_uri != NULL ; ++server_uri )
        {
            EventdRelayServer *server;
            server = g_hash_table_lookup(context->servers, *server_uri);
            if ( server == NULL )
            {
                server = eventd_relay_server_new_for_host_and_port(*server_uri);
                if ( server == NULL )
                    g_warning("Couldn't create the connectiont to server '%s'", *server_uri);
                else
                    g_hash_table_insert(context->servers, g_strdup(*server_uri), server);
            }
            if ( server != NULL )
                list = g_list_prepend(list, server);
        }
        g_strfreev(server_uris);
    }

#ifdef ENABLE_AVAHI
    gchar **avahi_names;
    if ( ( context->avahi != NULL ) && ( libeventd_config_key_file_get_string_list(config_file, "Relay", "Avahi", &avahi_names, NULL) == 0 ) )
    {
        gchar **avahi_name;
        for ( avahi_name = avahi_names ; *avahi_name != NULL ; ++avahi_name )
        {
            EventdRelayServer *server;
            server = g_hash_table_lookup(context->servers, *avahi_name);
            if ( server == NULL )
            {
                server = eventd_relay_server_new();
                eventd_relay_avahi_server_new(context->avahi, *avahi_name, server);
                g_hash_table_insert(context->servers, g_strdup(*avahi_name), server);
            }
            if ( server != NULL )
                list = g_list_prepend(list, server);
        }
        g_strfreev(avahi_names);
    }
#endif /* ENABLE_AVAHI */

    g_hash_table_insert(context->events, g_strdup(id), list);
}

static void
_eventd_relay_config_reset(EventdPluginContext *context)
{
    g_hash_table_remove_all(context->events);
}


/*
 * Event action interface
 */

static void
_eventd_relay_event_action(EventdPluginContext *context, const gchar *config_id, EventdEvent *event)
{
    GList *servers;
    GList *server;

    servers = g_hash_table_lookup(context->events, config_id);
    if ( servers == NULL )
        return;

    for ( server = servers ; server != NULL ; server = g_list_next(server) )
        eventd_relay_server_event(server->data, event);
}


/*
 * Plugin interface
 */

EVENTD_EXPORT const gchar *eventd_plugin_id = "eventd-relay";
EVENTD_EXPORT
void
eventd_plugin_get_interface(EventdPluginInterface *interface)
{
    libeventd_plugin_interface_add_init_callback(interface, _eventd_relay_init);
    libeventd_plugin_interface_add_uninit_callback(interface, _eventd_relay_uninit);

    libeventd_plugin_interface_add_start_callback(interface, _eventd_relay_start);
    libeventd_plugin_interface_add_stop_callback(interface, _eventd_relay_stop);

    libeventd_plugin_interface_add_control_command_callback(interface, _eventd_relay_control_command);

    libeventd_plugin_interface_add_event_parse_callback(interface, _eventd_relay_event_parse);
    libeventd_plugin_interface_add_config_reset_callback(interface, _eventd_relay_config_reset);

    libeventd_plugin_interface_add_event_action_callback(interface, _eventd_relay_event_action);
}

