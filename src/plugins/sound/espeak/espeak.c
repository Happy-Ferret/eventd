/*
 * eventd - Small daemon to act on remote or local events
 *
 * Copyright © 2011 Quentin "Sardem FF7" Glidic
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

#include <speak_lib.h>

#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <eventd-plugin.h>
#include <libeventd-event.h>
#include <libeventd-client.h>
#include <libeventd-config.h>
#include <libeventd-regex.h>

#include "../pulseaudio.h"

#include "espeak.h"
#include "pulseaudio.h"

static GHashTable *events = NULL;

typedef struct {
    guchar *data;
    gsize length;
} EventdSoundEspeakSoundData;

#define BUFFER_SIZE 2000

static void
_eventd_sound_espeak_callback_data_ref(EventdSoundEspeakCallbackData *data)
{
    ++data->ref_count;
}

static void
_eventd_sound_espeak_callback_data_free(EventdSoundEspeakCallbackData *data)
{
    EventdSoundEspeakSoundData *sound_data;

    switch ( data->mode )
    {
    case EVENTD_CLIENT_MODE_PING_PONG:
        sound_data = data->data;
        g_free(sound_data->data);
        g_free(sound_data);
    break;
    default:
        eventd_sound_espeak_pulseaudio_pa_data_free(data->data);
    break;
    }

    g_free(data);
}

static void
_eventd_sound_espeak_callback_data_unref(EventdSoundEspeakCallbackData *data)
{
    if ( --data->ref_count > 0 )
        return;

    _eventd_sound_espeak_callback_data_free(data);
}

static EventdSoundEspeakCallbackData *
_eventd_sound_espeak_callback_data_new(EventdClientMode mode)
{
    EventdSoundEspeakCallbackData *data;
    EventdSoundEspeakSoundData *sound_data;

    data = g_new0(EventdSoundEspeakCallbackData, 1);

    data->ref_count = 1;
    data->mode = mode;
    switch ( mode )
    {
    case EVENTD_CLIENT_MODE_PING_PONG:
        _eventd_sound_espeak_callback_data_ref(data);
        sound_data = g_new0(EventdSoundEspeakSoundData, 1);
        sound_data->data = g_malloc0_n(0, sizeof(guchar));
        data->data = sound_data;
    break;
    default:
        data->data = eventd_sound_espeak_pulseaudio_pa_data_new();
    break;
    }

    return data;
}

static int
synth_callback(gshort *wav, gint numsamples, espeak_EVENT *event)
{
    EventdSoundEspeakCallbackData *data;
    data = event->user_data;

    switch ( data->mode )
    {
    case EVENTD_CLIENT_MODE_PING_PONG:
        if ( wav != NULL )
        {
            EventdSoundEspeakSoundData *sound_data;
            gsize base;

            sound_data = data->data;
            base = sound_data->length;
            sound_data->length += numsamples * sizeof(gshort);
            sound_data->data = g_realloc_n(sound_data->data, sound_data->length, sizeof(guchar));
            memcpy(sound_data->data+base, wav, numsamples*sizeof(gshort));
        }
    break;
    default:
        eventd_sound_espeak_pulseaudio_play_data(wav, numsamples, event);
    break;
    }

    if ( ( wav == NULL ) && ( event->type == espeakEVENT_LIST_TERMINATED ) )
        _eventd_sound_espeak_callback_data_unref(event->user_data);

    return 0;
}

static int
uri_callback(int type, const char *uri, const char *base)
{
    return 1;
}

void
_eventd_sound_espeak_start(gpointer user_data)
{
    EventdSoundPulseaudioContext *context = user_data;
    gint sample_rate;

    sample_rate = espeak_Initialize(AUDIO_OUTPUT_RETRIEVAL, BUFFER_SIZE, NULL, 0);

    if ( sample_rate == EE_INTERNAL_ERROR )
    {
        g_warning("Couldn’t initialize eSpeak system");
        return;
    }

    espeak_SetSynthCallback(synth_callback);
    espeak_SetUriCallback(uri_callback);

    eventd_sound_espeak_pulseaudio_start(context, sample_rate);

    libeventd_regex_init();
}

void
_eventd_sound_espeak_stop()
{
    libeventd_regex_clean();

    espeak_Synchronize();
    espeak_Terminate();
}

static void
_eventd_sound_espeak_event_parse(const gchar *client_type, const gchar *event_name, GKeyFile *config_file)
{
    gboolean disable;
    gchar *message = NULL;
    gchar *name = NULL;
    gchar *parent = NULL;

    if ( ! g_key_file_has_group(config_file, "espeak") )
        return;

    if ( libeventd_config_key_file_get_boolean(config_file, "espeak", "disable", &disable) < 0 )
        return;
    if ( libeventd_config_key_file_get_string(config_file, "espeak", "message", &message) < 0 )
        return;

    name = libeventd_config_events_get_name(client_type, event_name);
    if ( event_name != NULL )
        parent = g_hash_table_lookup(events, client_type);

    if ( ! message )
        message = g_strdup(parent ?: "$event-data[text]");

    if ( disable )
    {
        g_free(name);
        g_free(message);
    }
    else
        g_hash_table_insert(events, name, message);
}

static gboolean
_eventd_sound_espeak_regex_event_data_cb(const GMatchInfo *info, GString *r, gpointer event_data)
{
    gchar *name;
    gchar *data = NULL;

    name = g_match_info_fetch(info, 1);
    if ( event_data != NULL )
    {
        gchar *lang_name;
        gchar *lang_data = NULL;
        gchar *tmp = NULL;

        data = g_hash_table_lookup(event_data, name);

        lang_name = g_strconcat(name, "-lang", NULL);
        lang_data = g_hash_table_lookup(event_data, lang_name);
        g_free(lang_name);

        if ( lang_data != NULL )
        {
            tmp = data;
            data = g_strdup_printf("<voice name=\"%s\">%s</voice>", lang_data, tmp);
        }
        else
            data = g_strdup(data);
    }
    g_free(name);

    g_string_append(r, data ?: "");
    g_free(data);

    return FALSE;
}

static GHashTable *
_eventd_sound_espeak_event_action(EventdClient *client, EventdEvent *event)
{
    GHashTable *ret = NULL;
    EventdClientMode client_mode;
    gchar *message;
    gchar *msg;
    espeak_ERROR error;
    EventdSoundEspeakCallbackData *data;
    EventdSoundEspeakSoundData *sound_data;

    client_mode = libeventd_client_get_mode(client);

    message = libeventd_config_events_get_event(events, libeventd_client_get_type(client), eventd_event_get_name(event));
    if ( message == NULL )
        return NULL;

    msg = libeventd_regex_replace_event_data(message, eventd_event_get_data(event), _eventd_sound_espeak_regex_event_data_cb);

    data = _eventd_sound_espeak_callback_data_new(client_mode);
    error = espeak_Synth(msg, strlen(msg)+1, 0, POS_CHARACTER, 0, espeakCHARS_UTF8|espeakSSML, NULL, data);

    switch ( error )
    {
    case EE_INTERNAL_ERROR:
        g_warning("Couldn’t synthetise text");
        _eventd_sound_espeak_callback_data_free(data);
        goto fail;
    case EE_BUFFER_FULL:
    case EE_OK:
    default:
    break;
    }

    switch ( client_mode )
    {
    case EVENTD_CLIENT_MODE_PING_PONG:
        espeak_Synchronize();
        sound_data = data->data;
        ret = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        g_hash_table_insert(ret, g_strdup("message"), g_strdup(msg));
        g_hash_table_insert(ret, g_strdup("audio-data"), g_base64_encode(sound_data->data, sound_data->length));
        _eventd_sound_espeak_callback_data_unref(data);
    break;
    default:
    break;
    }

fail:
    g_free(msg);

    return ret;
}

static void
_eventd_sound_espeak_config_init()
{
    events = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

static void
_eventd_sound_espeak_config_clean()
{
    g_hash_table_unref(events);
}

void
eventd_plugin_get_info(EventdPlugin *plugin)
{
    plugin->id = "espeak";

    plugin->start = _eventd_sound_espeak_start;
    plugin->stop = _eventd_sound_espeak_stop;

    plugin->config_init = _eventd_sound_espeak_config_init;
    plugin->config_clean = _eventd_sound_espeak_config_clean;

    plugin->event_parse = _eventd_sound_espeak_event_parse;
    plugin->event_action = _eventd_sound_espeak_event_action;
}
