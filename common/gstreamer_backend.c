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
    user_data->configuration->port = 5000;
    user_data->configuration->uiRefreshRate = -1;
    
    // Other init flags
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
    free(user_data->configuration);
    user_data->configuration = NULL;
    
    free(user_data);
    user_data = NULL;
}

/**********************
 VIDEO HANDLING METHODS
 *********************/
void create_video_sink_bin(RctGstUserData *user_data)
{
    // Create elements
    user_data->video_sink_bin = gst_bin_new("video-sink-bin");
    user_data->video_sink = gst_element_factory_make("glimagesink", "video-sink");
    // g_object_set(user_data->video_sink, "sync", FALSE, NULL);
    GstElement *video_convert = gst_element_factory_make("videoconvert", "video_convert");
    GstElement *video_post_queue = gst_element_factory_make("queue", "video_post_queue");

    // Add them
    gst_bin_add_many(user_data->video_sink_bin,
                     video_convert,
                     video_post_queue,
                     user_data->video_sink,
                     NULL
                     );
  
  
    g_object_set(user_data->video_sink, "sync", FALSE, NULL);
    
    // Link them
    if(!gst_element_link(video_convert, video_post_queue))
        rct_gst_log(user_data, g_strdup("RCTGstPlayer : Failed to link video_convert and video_post_queue\n"));
    
    if(!gst_element_link(video_post_queue, user_data->video_sink))
        rct_gst_log(user_data, g_strdup("RCTGstPlayer : Failed to link video_post_queue and video_sink\n"));

    
    // Creating ghostpad for playbin
    gst_element_add_pad(
                        GST_BIN(user_data->video_sink_bin),
                        gst_ghost_pad_new("sink", gst_element_get_static_pad(video_convert, "sink"))
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
    rct_gst_log(user_data, g_strdup("EOS\n"));
    
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
            // the values are packed into GValueArrays with the value per channel
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
        g_object_set(source, "latency", (guint)200, NULL);
    }

    if (rct_gst_element_has_attribute(user_data->source, "timeout")) {
        g_object_set(source, "timeout", (guint64)1000000, NULL);
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
    /*
    if (user_data->current_state == GST_STATE_PLAYING) {
        // If we didn't know it yet, query the stream duration
        if (!GST_CLOCK_TIME_IS_VALID (user_data->duration))
            gst_element_query_duration(user_data->pipeline, GST_FORMAT_TIME, &user_data->duration);
        
        if (gst_element_query_position(user_data->pipeline, GST_FORMAT_TIME, &user_data->position)) {
            if (user_data->configuration->onPlayingProgress)
                user_data->configuration->onPlayingProgress(user_data->configuration->owner, user_data->position / GST_MSECOND, user_data->duration / GST_MSECOND);
        }
    }

     */
    return TRUE;
}

static void cb_state_changed(GstBus *bus, GstMessage *msg, RctGstUserData* user_data)
{
    GstState old_state, new_state, pending_state;
    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
    
    // Message coming from the video_sink_bin
    if(GST_MESSAGE_SRC(msg) == GST_OBJECT(user_data->video_sink_bin) && new_state != old_state && user_data->configuration != NULL) {
        if (new_state == GST_STATE_NULL && old_state == GST_STATE_READY) {
            gst_element_sync_state_with_parent(user_data->video_sink_bin);
        }
    }

    // Message coming from the playbin
    else if(GST_MESSAGE_SRC(msg) == GST_OBJECT(user_data->pipeline) && new_state != old_state && user_data->configuration != NULL) {
        
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

                if (user_data->configuration->onPlayerInit) {
                    user_data->configuration->onPlayerInit(user_data->configuration->owner);
                }
            }
            
            rct_gst_set_playbin_state(user_data, GST_STATE_READY);
            
            if (old_state == GST_STATE_PAUSED) {
                gst_element_set_state(user_data->video_sink_bin, GST_STATE_NULL);
            }
        }
        
        // Get element duration if not known yet
        if (new_state >= GST_STATE_PAUSED) {
            if (!GST_CLOCK_TIME_IS_VALID (user_data->duration)) {
                gst_element_query_duration (user_data->pipeline, GST_FORMAT_TIME, &user_data->duration);
                
                if (user_data->configuration->onPlayingProgress) {
                    user_data->configuration->onPlayingProgress(user_data->configuration->owner, 0, user_data->duration / GST_MSECOND);
                }
            }
        }
        
        // Seek if requested
        if (new_state == GST_STATE_PLAYING && user_data->desired_position != GST_CLOCK_TIME_NONE) {
            if (GST_CLOCK_TIME_IS_VALID(user_data->desired_position)) {
                
                // Restore good base time as we never stored the right one
                GstClockTime base_time = gst_element_get_base_time(user_data->pipeline);
                gst_element_set_base_time(user_data->pipeline, base_time - user_data->desired_position);
                
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
    
    gst_element_get_state(user_data->pipeline, &current_state, &pending_state, 0);
    
    rct_gst_log(user_data,
            g_strdup_printf(
                "Actual state : %s - Pipeline state requested : %s - Pending state : %s\n",
                gst_element_state_get_name(current_state),
                gst_element_state_get_name(state),
                gst_element_state_get_name(pending_state),
                NULL
            )
        );
    
    if (user_data->pipeline != NULL && current_state != state) {
        if (pending_state != state) {
            validity = gst_element_set_state(user_data->pipeline, state);
        } else {
            rct_gst_log(user_data, g_strdup("RCTGstPlayer : Ignoring state change (Already changing to requested state)\n"));
        }
    }
    return validity;
}

void on_pad_added(GstElement *gstelement, GstPad *new_pad, RctGstUserData *user_data)
{
    GstCaps *caps = NULL;
    GstStructure *structure;
    
    caps = gst_pad_get_current_caps(new_pad);
    if (!caps)
        caps = gst_pad_query_caps(new_pad, NULL);
    
    structure = gst_caps_get_structure(caps, 0);
    
    const gchar *name = gst_structure_get_string(structure, "media");
    if (!g_strcmp0(name, "video")) {
        rct_gst_log(user_data, g_strdup("Video pad added\n"));
        gst_pad_link(new_pad, gst_element_get_static_pad(user_data->video_queue, "sink"));
    }
    g_object_ref(new_pad);
    
    if (user_data->configuration->onPadAdded) {
        gchar *name;
        
        name = gst_pad_get_name(new_pad);
        user_data->configuration->onPadAdded(user_data->configuration->owner, name);
        g_free(name);
    }
}

void on_decoder_pad_added(GstElement *gstelement, GstPad *new_pad, RctGstUserData *user_data)
{
    GstCaps *caps = gst_pad_get_current_caps(new_pad);
    rct_gst_log(user_data, g_strdup(gst_caps_to_string(caps)));
    gst_pad_link(new_pad, gst_element_get_static_pad(user_data->video_sink_bin, "sink"));
    
    g_object_ref(new_pad);
}

void rct_gst_init(RctGstUserData* user_data)
{
    // Create a playbin pipeline
    GError *error = NULL;
    
    user_data->pipeline = gst_pipeline_new("pipeline");
    if (error != NULL)
    {
        g_print ("Could not construct pipeline: %s\n", error->message);
    }
    
    // Video components
    user_data->source = gst_element_factory_make("udpsrc", "src");
    g_object_set(user_data->source, "port", user_data->configuration->port, NULL);
  
    //GstElement *capsfilter = gst_element_factory_make("capsfilter", NULL);
    GstCaps *caps = gst_caps_from_string("application/x-rtp"); //, media=(string)video, clock-rate=(int)90000, width=(int)720, height=(int)480, encoding-name=(string)H264, payload=(int)96
    g_object_set(user_data->source, "caps", caps, NULL);
    gst_caps_unref(caps);
  
    cb_setup_source(user_data->pipeline, user_data->source, user_data);
    
    user_data->video_queue = gst_element_factory_make("queue", "video_queue");
    user_data->video_depay = gst_element_factory_make("rtptheoradepay", "rtpmp4vdepay");

    user_data->decodebin = gst_element_factory_make("theoradec", "avdec_mpeg4");

    create_video_sink_bin(user_data);

    gst_bin_add_many(GST_BIN(user_data->pipeline),
                     user_data->source,
                     user_data->video_queue,
                     user_data->video_depay,
                     user_data->decodebin,
                     user_data->video_sink_bin,
                     NULL);

    gst_element_link_many(user_data->source,
                          user_data->video_queue,
                          user_data->video_depay,
                          user_data->decodebin,
                          user_data->video_sink_bin,
                          NULL);

    g_signal_connect(user_data->decodebin, "pad-added", G_CALLBACK(on_decoder_pad_added), user_data);


    /*// Audio components
    user_data->audio_queue = gst_element_factory_make("queue", "audio_queue");
    user_data->audio_depay = gst_element_factory_make("rtpvorbisdepay", "rtpvorbisdepay");
    GstElement *vorbisdec = gst_element_factory_make("vorbisdec", "vorbisdec");
    GstElement *audio_convert = gst_element_factory_make("audioconvert", "audioconvert");
    GstElement *audio_resample = gst_element_factory_make("audioresample", "audioresample");
    create_audio_sink_bin(user_data);
    
    GstElement *audio_post_queue = gst_element_factory_make("queue", "audio_post_queue");

    gst_bin_add_many(user_data->pipeline,
                     user_data->audio_queue,
                     user_data->audio_depay,
                     vorbisdec,
                     audio_convert,
                     audio_resample,
                     audio_post_queue,
                     user_data->audio_sink_bin,
                     NULL);

    gst_element_link_many(user_data->audio_queue,
                          user_data->audio_depay,
                          vorbisdec,
                          audio_convert,
                          audio_resample,
                          audio_post_queue,
                          user_data->audio_sink_bin,
                          NULL);*/

    // Pad creation
    g_signal_connect(user_data->source, "pad-added", G_CALLBACK(on_pad_added), user_data);


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
    user_data->bus = gst_element_get_bus(user_data->pipeline);
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
    gst_element_set_state (user_data->pipeline, GST_STATE_NULL);
    g_main_loop_quit(user_data->main_loop);
}

gchar *rct_gst_get_info()
{
    return gst_version_string();
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
                                user_data->pipeline,
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

void rct_gst_set_ui_refresh_rate(RctGstUserData* user_data, guint64 uiRefreshRate)
{
    user_data->configuration->uiRefreshRate = uiRefreshRate;

    if (user_data->is_ready) {

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
    /*
    if (user_data->configuration->onElementLog) {
        user_data->configuration->onElementLog(user_data->configuration->owner, message);
    }
    */
    
    g_free(message);
}
