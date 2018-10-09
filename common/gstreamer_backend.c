//
//  gstreamer_backend.c
//
//  Created by Alann on 13/12/2017.
//  Copyright © 2017 Kalyzee. All rights reserved.
//

#include "gstreamer_backend.h"
#include <stdlib.h>

// User data
RctGstUserData *rct_gst_init_user_data()
{
    // Create user data structure
    RctGstUserData *user_data = calloc(1, sizeof(RctGstUserData));
    
    // Create configuration structure
    user_data->configuration = calloc(1, sizeof(RctGstConfiguration));
    user_data->configuration->uri = "";
    user_data->configuration->uiRefreshRate = -1;
    
    // Other init flags
    user_data->must_apply_uri = FALSE;
    user_data->is_ready = FALSE;
    user_data->current_state = GST_STATE_VOID_PENDING;
    user_data->must_clear_screen = FALSE;
    
    user_data->duration = GST_CLOCK_TIME_NONE;
    user_data->position = GST_CLOCK_TIME_NONE;
    user_data->desired_position = GST_CLOCK_TIME_NONE;
    user_data->last_seek_time = gst_util_get_timestamp();
    
    return user_data;
}

void rct_gst_free_user_data(RctGstUserData *user_data)
{
    free(user_data->configuration->uri);
    user_data->configuration->uri = NULL;
    
    free(user_data->configuration);
    user_data->configuration = NULL;
    
    free(user_data);
    user_data = NULL;
}

/**********************
 AUDIO HANDLING METHODS
 *********************/
void create_audio_sink_bin(RctGstUserData *user_data)
{
    // Create elements
    user_data->audio_sink_bin = gst_bin_new("audio-sink-bin");
    user_data->volume_controller = gst_element_factory_make("volume", "audio-volume");
    user_data->audio_level_analyser = gst_element_factory_make("level", "audio-level-analyser");
    user_data->audio_sink = gst_element_factory_make("autoaudiosink", "audio-sink");
    
    // Add them
    gst_bin_add_many(GST_BIN(user_data->audio_sink_bin),
                     user_data->volume_controller,
                     user_data->audio_level_analyser,
                     user_data->audio_sink,
                     NULL);


    // Link them
    
    if(!gst_element_link(user_data->volume_controller, user_data->audio_level_analyser))
        rct_gst_log(user_data, "RCTGstPlayer : Failed to link volume-controller and audio-level-analyser\n");
    
    if(!gst_element_link(user_data->audio_level_analyser, user_data->audio_sink))
        rct_gst_log(user_data, "RCTGstPlayer : Failed to link audio-level-analyser and audio-sink\n");

    // Creating ghostpad for uri-decode-bin
    gst_element_add_pad(GST_BIN(user_data->audio_sink_bin), gst_ghost_pad_new("sink", gst_element_get_static_pad(user_data->volume_controller, "sink")));
}

/**********************
 VIDEO HANDLING METHODS
 *********************/
void create_video_sink_bin(RctGstUserData *user_data)
{
    // Create elements
    user_data->video_sink_bin = gst_bin_new("video-sink-bin");
    user_data->video_sink = gst_element_factory_make("glimagesink", "video-sink");

    // Add them
    gst_bin_add_many(
                     user_data->video_sink_bin,
                     user_data->video_sink,
                     NULL
                     );
    
    // Creating ghostpad for playbin
    gst_element_add_pad(
                        GST_BIN(user_data->video_sink_bin),
                        gst_ghost_pad_new("sink", gst_element_get_static_pad(user_data->video_sink, "sink"))
                        );
}

/*********************
 APPLICATION CALLBACKS
 ********************/

static GstBusSyncReply cb_create_window(GstBus *bus, GstMessage *message, RctGstUserData* user_data)
{
    if(!gst_is_video_overlay_prepare_window_handle_message(message))
        return GST_BUS_PASS;
    
    if (user_data->video_overlay) {
        gst_video_overlay_set_window_handle(user_data->video_overlay, user_data->configuration->drawableSurface);
    }
    
    gst_message_unref(message);
    return GST_BUS_DROP;
}

static void cb_error(GstBus *bus, GstMessage *msg, RctGstUserData* user_data)
{
    GError *err;
    gchar *debug_info;
    
    gst_message_parse_error(msg, &err, &debug_info);
    rct_gst_log(user_data,
        g_strdup_printf("RCTGstPlayer : Error received from element %s: %s\n",
            GST_OBJECT_NAME(msg->src),
            err->message,
            NULL
        )
    );
    
    if (user_data->configuration->onElementError) {
        user_data->configuration->onElementError(user_data->configuration->owner, GST_OBJECT_NAME(msg->src), err->message, debug_info);
    }
    
    g_clear_error(&err);
    g_free(debug_info);
}

static void cb_eos(GstBus *bus, GstMessage *msg, RctGstUserData* user_data)
{
    rct_gst_log(user_data, "EOS\n");
    
    if (user_data->configuration->onEOS) {
        user_data->configuration->onEOS(user_data->configuration->owner);
    }
}

static gboolean cb_message_element(GstBus *bus, GstMessage *msg, RctGstUserData* user_data)
{
    if(msg->type == GST_MESSAGE_ELEMENT)
    {
        const GstStructure *s = gst_message_get_structure(msg);
        const gchar *name = gst_structure_get_name(s);
        
        RctGstAudioLevel *audio_channels_level;
        
        GValueArray *rms_arr, *peak_arr, *decay_arr;
        gdouble rms_dB, peak_dB, decay_dB;
        const GValue *value;

        if(g_strcmp0(name, "level") == 0)
        {
            /* the values are packed into GValueArrays with the value per channel */
            const GValue *array_val = gst_structure_get_value(s, "peak");
            
            array_val = gst_structure_get_value(s, "rms");
            rms_arr = (GValueArray *)g_value_get_boxed(array_val);
            
            array_val = gst_structure_get_value(s, "peak");
            peak_arr = (GValueArray *)g_value_get_boxed(array_val);
            
            array_val = gst_structure_get_value(s, "decay");
            decay_arr = (GValueArray *)g_value_get_boxed(array_val);
            
            // No multichannel needs to be handled - Otherwise : gint channels = rms_arr->n_values;
            gint nb_channels = rms_arr->n_values;
            
            if (nb_channels > 0) {
                audio_channels_level = calloc(nb_channels, sizeof(RctGstAudioLevel));

                int i = 0;
                for (i = 0; i < nb_channels; i++) {
                    // Create audio level structure
                    RctGstAudioLevel *audio_level_structure = &audio_channels_level[i];
                    
                    // RMS
                    value = g_value_array_get_nth(rms_arr, i);
                    rms_dB = g_value_get_double(value);
                    audio_level_structure->rms = rms_dB; //pow(10, rms_dB / 20); // converting from dB to normal gives us a value between 0.0 and 1.0
                    
                    // PEAK
                    value = g_value_array_get_nth(peak_arr, i);
                    peak_dB = g_value_get_double(value);
                    audio_level_structure->peak = peak_dB; //pow(10, peak_dB / 20); // converting from dB to normal gives us a value between 0.0 and 1.0
                    
                    // DECAY
                    value = g_value_array_get_nth(decay_arr, i);
                    decay_dB = g_value_get_double(value);
                    audio_level_structure->decay = decay_dB; // pow(10, decay_dB / 20); // converting from dB to normal gives us a value between 0.0 and 1.0
                }
                
                if (user_data->configuration->onVolumeChanged) {
                    user_data->configuration->onVolumeChanged(user_data->configuration->owner, audio_channels_level, nb_channels);
                }
            }
            
            free(audio_channels_level);
        }
    }
    return TRUE;
}

static gboolean cb_message_buffering(GstBus *bus, GstMessage *msg, RctGstUserData* user_data)
{
    if (user_data->configuration->onBufferingProgress) {
        gint percent;
        gst_message_parse_buffering (msg, &percent);
        
        user_data->configuration->onBufferingProgress(user_data->configuration->owner, percent);
    }

    return TRUE;
}

static void cb_new_pad(GstElement *element, GstPad *pad, RctGstUserData* user_data)
{
    if (user_data->configuration->onPadAdded) {
        gchar *name;

        name = gst_pad_get_name(pad);
        user_data->configuration->onPadAdded(user_data->configuration->owner, name);
        g_free(name);
    }
}

// Remove latency as much as possible
static void cb_setup_source(GstElement *pipeline, GstElement *source, RctGstUserData* user_data) {

    if (rct_gst_element_has_attribute(user_data->source, "latency")) {
        g_object_set(source, "latency", (guint)250, NULL);
    }

    if (rct_gst_element_has_attribute(user_data->source, "timeout")) {
        g_object_set(source, "timeout", (guint64)5000000, NULL);
    }

    if (rct_gst_element_has_attribute(user_data->source, "tcp-timeout")) {
        g_object_set(source, "tcp-timeout", (guint64)4000000, NULL);
    }

    if (rct_gst_element_has_attribute(user_data->source, "retry")) {
        g_object_set(source, "retry", (guint)65535, NULL);
    }
}

static gboolean cb_duration_and_progress(RctGstUserData* user_data)
{
    if (user_data->current_state == GST_STATE_PLAYING) {
        // If we didn't know it yet, query the stream duration
        if (!GST_CLOCK_TIME_IS_VALID (user_data->duration))
            gst_element_query_duration(user_data->playbin, GST_FORMAT_TIME, &user_data->duration);
        
        if (gst_element_query_position(user_data->playbin, GST_FORMAT_TIME, &user_data->position)) {
            if (user_data->configuration->onPlayingProgress)
                user_data->configuration->onPlayingProgress(user_data->configuration->owner, user_data->position / GST_MSECOND, user_data->duration / GST_MSECOND);
        }
    }
    
    return TRUE;
}

static void cb_state_changed(GstBus *bus, GstMessage *msg, RctGstUserData* user_data)
{
    GstState old_state, new_state, pending_state;
    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);

    // Message coming from the playbin
    if(GST_MESSAGE_SRC(msg) == GST_OBJECT(user_data->playbin) && new_state != old_state && user_data->configuration != NULL) {
        
        user_data->current_state = new_state;
        
        rct_gst_log(user_data,
                    g_strdup_printf(
                                    "RCTGstPlayer : New playbin state %s\n",
                                    gst_element_state_get_name(new_state),
                                    NULL
                    )
        );
        
        if (new_state == GST_STATE_READY) {
            
            if (!user_data->is_ready) {
                user_data->is_ready = TRUE;
                
                // Get video overlay
                user_data->video_overlay = GST_VIDEO_OVERLAY(user_data->video_sink);
                // g_object_unref(user_data->video_sink);

                if (user_data->configuration->onPlayerInit) {
                    user_data->configuration->onPlayerInit(user_data->configuration->owner);
                }
            }
            
            // We apply the uri when the uri-decode-bin is in ready state and an uri is set if needed (async step to ready)
            if (g_strcmp0("", user_data->configuration->uri) != 0) {
                if (user_data->must_apply_uri) {
                    rct_gst_apply_uri(user_data);
                    user_data->must_apply_uri = FALSE;
                }
            }
        }
        
        if (new_state >= GST_STATE_READY) {
            rct_gst_set_volume(user_data, user_data->configuration->volume);
        }
        
        // Get element duration if not known yet
        if (new_state >= GST_STATE_PAUSED) {
            if (!GST_CLOCK_TIME_IS_VALID (user_data->duration)) {
                gst_element_query_duration (user_data->playbin, GST_FORMAT_TIME, &user_data->duration);
                
                if (user_data->configuration->onPlayingProgress) {
                    user_data->configuration->onPlayingProgress(user_data->configuration->owner, 0, user_data->duration / GST_MSECOND);
                }
            }
        }
        
        // Seek if requested
        if (new_state == GST_STATE_PLAYING && user_data->desired_position != GST_CLOCK_TIME_NONE) {
            if (GST_CLOCK_TIME_IS_VALID(user_data->desired_position)) {
                
                // Restore good base time as we never stored the right one
                GstClockTime base_time = gst_element_get_base_time(user_data->playbin);
                gst_element_set_base_time(user_data->playbin, base_time - user_data->desired_position);
                
                execute_seek(user_data, user_data->desired_position);
            }
        }
        
        if (user_data->configuration->onStateChanged) {
            user_data->configuration->onStateChanged(user_data->configuration->owner, old_state, new_state);
        }
    }
}

static gboolean cb_delayed_seek(RctGstUserData* user_data) {
    execute_seek(user_data, user_data->desired_position);
    return FALSE;
}

static gboolean cb_bus_watch(GstBus *bus, GstMessage *message, RctGstUserData* user_data)
{
    switch (GST_MESSAGE_TYPE(message))
    {
        case GST_MESSAGE_ERROR:
            cb_error(bus, message, user_data);
            break;
            
        case GST_MESSAGE_EOS:
            cb_eos(bus, message, user_data);
            break;
            
        case GST_MESSAGE_STATE_CHANGED:
            cb_state_changed(bus, message, user_data);
            break;
            
        case GST_MESSAGE_ELEMENT:
            cb_message_element(bus, message, user_data);
            break;
            
        case GST_MESSAGE_BUFFERING:
            cb_message_buffering(bus, message, user_data);
            break;
            
        case GST_MESSAGE_DURATION_CHANGED:
            user_data->duration = GST_CLOCK_TIME_NONE;
            cb_duration_and_progress(user_data);
            break;
            
        default:
            break;
    }
    
    return TRUE;
}

/*************
 OTHER METHODS
 ************/
GstStateChangeReturn rct_gst_set_playbin_state(RctGstUserData* user_data, GstState state)
{
    GstStateChangeReturn validity = GST_STATE_CHANGE_FAILURE;
    GstState current_state;
    GstState pending_state;
    
    gst_element_get_state(user_data->playbin, &current_state, &pending_state, 0);
    
    rct_gst_log(user_data,
            g_strdup_printf(
                "Actual state : %s - Pipeline state requested : %s - Pending state : %s\n",
                gst_element_state_get_name(current_state),
                gst_element_state_get_name(state),
                gst_element_state_get_name(pending_state),
                NULL
            )
        );
    
    if (user_data->playbin != NULL && current_state != state) {
        if (pending_state != state) {
            validity = gst_element_set_state(user_data->playbin, state);
        } else {
            rct_gst_log(user_data, "RCTGstPlayer : Ignoring state change (Already changing to requested state)\n");
        }
    }
    return validity;
}

void on_pad_added(GstElement *gstelement, GstPad *new_pad, gpointer user_data)
{
    rct_gst_log(user_data, "PAD ADDED");
}

void rct_gst_init(RctGstUserData* user_data)
{
    // Create a playbin pipeline
    // user_data->playbin = gst_element_factory_make("playbin", "playbin");
    GError *error = NULL;
    user_data->playbin = gst_parse_launch("rtspsrc name=src "
                                          "src. ! rtph264depay ! h264parse ! vtdec name=h264dec "
                                          "src. ! rtpvorbisdepay ! vorbisdec name=vorbisdec",
                                          &error);cd 
    
    
    if (error != NULL)
    {
        g_print ("Could not construct pipeline: %s\n", error->message);
        return;
    }
    
    gst_debug_set_threshold_for_name("rtspsrc", GST_LEVEL_WARNING);
    gst_debug_set_threshold_for_name("updsrc", GST_LEVEL_WARNING);
    
    user_data->source = gst_bin_get_by_name(user_data->playbin, "src");
        
    cb_setup_source(user_data->playbin, user_data->source, user_data);
    
    // Create audio with level analyser
    create_audio_sink_bin(user_data);
    gst_bin_add(user_data->playbin, user_data->audio_sink_bin);
    GstElement *vorbisdec = gst_bin_get_by_name(GST_BIN(user_data->playbin), "vorbisdec");
    gst_element_link(vorbisdec, user_data->audio_sink_bin);

    // Create video
    create_video_sink_bin(user_data);
    gst_bin_add(user_data->playbin, user_data->video_sink_bin);
    GstElement *h264dec = gst_bin_get_by_name(GST_BIN(user_data->playbin), "h264dec");
    gst_element_link(h264dec, user_data->video_sink_bin);
    

    // Test purposes
    /*
    GstElement *video_test_src = gst_element_factory_make("videotestsrc", "video-test-src");
    gst_bin_add_many (GST_BIN (user_data->playbin), video_test_src, user_data->video_sink_bin, NULL);
    gst_element_link(video_test_src, user_data->video_sink_bin);

    GstElement *audio_test_src = gst_element_factory_make("audiotestsrc", "audio-test-src");
    gst_bin_add_many (GST_BIN (user_data->playbin), audio_test_src, user_data->audio_sink_bin, NULL);
    gst_element_link(audio_test_src, user_data->audio_sink_bin);
     */

    // Preparing bus
    user_data->bus = gst_element_get_bus(user_data->playbin);
    user_data->bus_watch_id = gst_bus_add_watch(user_data->bus, cb_bus_watch, user_data);
    gst_bus_set_sync_handler(user_data->bus,(GstBusSyncHandler)cb_create_window, user_data, NULL);
    gst_object_unref(user_data->bus);
    
    // Create loop
    user_data->main_loop = g_main_loop_new(NULL, FALSE);
    
    rct_gst_set_playbin_state(user_data, GST_STATE_READY);
}

void rct_gst_run_loop(RctGstUserData* user_data)
{
    g_main_loop_run(user_data->main_loop);
    
    rct_gst_free_user_data(user_data);
    
    g_main_loop_unref(user_data->main_loop);
    
    g_source_remove(user_data->bus_watch_id);
}

void rct_gst_terminate(RctGstUserData* user_data)
{
    gst_element_set_state (user_data->playbin, GST_STATE_NULL);
    g_main_loop_quit(user_data->main_loop);
}

gchar *rct_gst_get_info()
{
    return gst_version_string();
}

static void rct_gst_apply_uri(RctGstUserData* user_data)
{
    rct_gst_log(user_data, "RCTGstPlayer : rct_gst_apply_uri\n");
    GstElement *src = gst_bin_get_by_name(GST_BIN(user_data->playbin), "src");
    g_object_set(src, "location", user_data->configuration->uri, NULL);
    
    if (user_data->configuration->onUriChanged) {
        user_data->configuration->onUriChanged(user_data->configuration->owner ,user_data->configuration->uri);
    }
}

static void execute_seek(RctGstUserData* user_data, gint64 position) {
    gint64 diff;
    
    if (position == GST_CLOCK_TIME_NONE)
        return;
    
    diff = gst_util_get_timestamp() - user_data->last_seek_time;
    
    if (GST_CLOCK_TIME_IS_VALID (user_data->last_seek_time) && diff < SEEK_MIN_DELAY) {
        /* The previous seek was too close, delay this one */
        GSource *timeout_source;
        
        if (user_data->desired_position == GST_CLOCK_TIME_NONE) {
            /* There was no previous seek scheduled. Setup a timer for some time in the future */
            timeout_source = g_timeout_source_new((SEEK_MIN_DELAY - diff) / GST_MSECOND);
            g_source_set_callback(timeout_source, (GSourceFunc)cb_delayed_seek, user_data, NULL);
            g_source_attach(timeout_source, NULL);
            g_source_unref(timeout_source);
        }
        /* Update the desired seek position. If multiple requests are received before it is time
         * to perform a seek, only the last one is remembered. */
        user_data->desired_position = position;
    } else {
        
        /* Perform the seek now */
        user_data->last_seek_time = gst_util_get_timestamp();
        gst_element_seek_simple(
                                user_data->playbin,
                                GST_FORMAT_TIME,
                                GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
                                position
                                );
        user_data->desired_position = GST_CLOCK_TIME_NONE;
    }
}

void rct_gst_seek(RctGstUserData *user_data, gint64 position) {
    
    position = position * GST_MSECOND;
    
    if (user_data->current_state >= GST_STATE_PAUSED) {
        execute_seek(user_data, position);
    } else {
        user_data->desired_position = position;
    }
}

// Setters
void rct_gst_set_uri(RctGstUserData* user_data, gchar *_uri)
{
    GstState current_state;
    
    // Update URI if it changed
    if (g_strcmp0(user_data->configuration->uri, _uri) != 0) {
        user_data->configuration->uri = _uri;
        
        rct_gst_log(user_data,
                    g_strdup_printf("RCTGstPlayer : rct_gst_set_uri : %s\n",
                                    user_data->configuration->uri,
                                    NULL
                    )
        );
        
        rct_gst_set_playbin_state(user_data, GST_STATE_READY);
        
        gst_element_get_state(user_data->playbin, &current_state, NULL, 0);
        if (current_state == GST_STATE_READY) {
            rct_gst_apply_uri(user_data);
        } else {
            user_data->must_apply_uri = TRUE;
        }
    }
}

void rct_gst_set_ui_refresh_rate(RctGstUserData* user_data, guint64 uiRefreshRate)
{
    user_data->configuration->uiRefreshRate = uiRefreshRate;

    if (user_data->is_ready) {

        g_object_set(user_data->audio_level_analyser, "interval", user_data->configuration->uiRefreshRate * GST_MSECOND, NULL);

        if (user_data->timeout_source) {
            g_source_destroy(user_data->timeout_source);
        }

        // Progression callback
        if (user_data->configuration->onPlayingProgress) {
            user_data->timeout_source = g_timeout_source_new(user_data->configuration->uiRefreshRate);
            g_source_set_callback(user_data->timeout_source, (GSourceFunc)cb_duration_and_progress, user_data, NULL);
            g_source_attach(user_data->timeout_source, NULL);
            g_source_unref(user_data->timeout_source);
        }
    }
}

void rct_gst_set_volume(RctGstUserData* user_data, gdouble volume)
{
    user_data->configuration->volume = volume;
    
    if (user_data->is_ready) {
        g_object_set(user_data->volume_controller, "volume", user_data->configuration->volume, NULL);
    }
}

void rct_gst_set_drawable_surface(RctGstUserData *user_data, guintptr drawable_surface)
{
    if (user_data->configuration->drawableSurface != drawable_surface) {
        user_data->configuration->drawableSurface = drawable_surface;
    }
    gst_video_overlay_prepare_window_handle(user_data->video_overlay);
}


// Utils
static gboolean rct_gst_element_has_attribute(GstElement *element, const gchar *attribute)
{
    GParamSpec* attribute_obj = NULL;
   
    if (GST_IS_OBJECT(element)) {
        GObjectClass *klass = G_OBJECT_GET_CLASS(element);
        attribute_obj = g_object_class_find_property(klass, attribute);
    }
    
    return (attribute_obj != NULL);
}

void rct_gst_log(RctGstUserData *user_data, gchar *message)
{
    if (user_data->configuration->onElementLog) {
        user_data->configuration->onElementLog(user_data->configuration->owner, message);
    }
}
