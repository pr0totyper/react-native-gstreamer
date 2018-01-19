//
//  gstreamer_backend.h
//
//  Created by Alann on 13/12/2017.
//  Copyright © 2017 Kalyzee. All rights reserved.
//

#ifndef gstreamer_backend_h
#define gstreamer_backend_h

#include <stdio.h>
#include <math.h>
#include <gst/gst.h>
#include <gst/video/video.h>

// Audio level definition
typedef struct {
    gdouble rms;
    gdouble peak;
    gdouble decay;
} RctGstAudioLevel;

// Plugin configurator
typedef struct
{
    gchar *uri;                                                     // Uri of the resource
    gint *audioLevelRefreshRate;                                    // Time in ms between each call of onVolumeChanged
    guintptr drawableSurface;                                       // Pointer to drawable surface
    gboolean isDebugging;                                           // Loads debugging pipeline
    
    // Callbacks
    void(*onInit)(void);                                            // Called when the player is ready
    void(*onStateChanged)(GstState old_state, GstState new_state);  // Called method when GStreamer state changes
    void(*onVolumeChanged)(RctGstAudioLevel* audioLevel);           // Called method when current media volume changes
    void(*onUriChanged)(gchar *new_uri);                            // Called when changing uri is over
    void(*onEOS)(void);                                             // Called when EOS occurs
    void(*onElementError)(gchar *source, gchar *message,            // Called when an error occurs
                          gchar *debug_info);
} RctGstConfiguration;

// User data definition
typedef struct {
    
    // Globals items
    RctGstAudioLevel* audio_level;
    RctGstConfiguration* configuration;
    
    GMainContext *context;
    GstElement *pipeline;
    GMainLoop *main_loop;
    guint bus_watch_id;
    GstBus *bus;
    
    // Video
    GstVideoOverlay* video_overlay;
    
    // Audio
    GstElement* audio_level_element;
    
    // Sinks
    GstElement *video_sink;
    GstElement *audio_sink;
} RctGstUserData;

// Setters
void rct_gst_set_drawable_surface(RctGstUserData* user_data, guintptr _drawableSurface);
void rct_gst_set_uri(RctGstUserData* user_data, gchar* _uri);
void rct_gst_set_audio_level_refresh_rate(RctGstUserData* user_data, gint rct_gst_set_audio_level_refresh_rate);
void rct_gst_set_debugging(RctGstUserData* user_data, gboolean is_debugging);

RctGstUserData *rct_gst_init_user_data();
void rct_gst_free_user_data(RctGstUserData* user_data);

// Other
GstStateChangeReturn rct_gst_set_pipeline_state(RctGstUserData* user_data, GstState state);
void rct_gst_init(RctGstUserData* user_data);
void rct_gst_run_loop(RctGstUserData* user_data);
void rct_gst_terminate(RctGstUserData* user_data);

gchar *rct_gst_get_info();
void rct_gst_apply_drawable_surface(RctGstUserData* user_data);
void rct_gst_apply_uri(RctGstUserData* user_data);

#endif /* gstreamer_backend_h */
