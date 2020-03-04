//
//  EaglUiView.m
//
//  Created by Alann on 13/12/2017.
//  Copyright © 2017 Kalyzee. All rights reserved.
//

#import "RCTGstPlayerView.h"

@implementation RCTGstPlayerView

static gboolean shareInstance;
static RCTGstPlayerView *instance;

// GL surface
+ (Class) layerClass
{
    return [CAEAGLLayer class];
}

+ (RCTGstPlayerView *)getView
{
    if (shareInstance) {
        if (!instance)
            instance = [[RCTGstPlayerView alloc] init];
    } else {
        instance = [[RCTGstPlayerView alloc] init];
    }
    
    NSLog(@"Returning View : %@", instance);
    return instance;
}

// Constructor
- (instancetype)init
{
    self = [super init];
    if (self) {
        self->pipelineState = NULL;
        self->is_view_ready = FALSE;
        self->userData = [self getUserData];
        self->port = 5000;
        self->volume = 0;
        self->refreshRate = 50;
        
        NSLog(@"Version : %s", rct_gst_get_info());
        
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(onInactiveState) name:UIApplicationWillResignActiveNotification object:nil];
        
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(onBackgroundState) name:UIApplicationDidEnterBackgroundNotification object:nil];
        
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(onActiveState) name:UIApplicationDidBecomeActiveNotification object:nil];
    }
    
    return self;
}

-(void)onInactiveState
{
    NSLog(@"Inactive state detected");
    rct_gst_set_playbin_state([self getUserData], GST_STATE_PAUSED);
}

-(void)onBackgroundState
{
    NSLog(@"Background state detected");
    rct_gst_set_playbin_state([self getUserData], GST_STATE_READY);
}

-(void)onActiveState
{
    NSLog(@"Active state detected");
    rct_gst_set_playbin_state([self getUserData], GST_STATE_PLAYING);
}

- (void)setRefreshRate:(guint64)refreshRate
{
    self->refreshRate = refreshRate;
    
    if (self->is_view_ready)
        rct_gst_set_ui_refresh_rate([self getUserData], refreshRate);
}

- (void) setViewReady:(gboolean) is_view_ready
{
    self->is_view_ready = is_view_ready;
    
    [self getUserData]->configuration->onPlayerInit = onPlayerInit;
    [self getUserData]->configuration->onPadAdded = onPadAdded;
    [self getUserData]->configuration->onEOS = onEOS;
    [self getUserData]->configuration->onPlayingProgress = onPlayingProgress;
    [self getUserData]->configuration->onBufferingProgress = onBufferingProgress;
    [self getUserData]->configuration->onElementError = onElementError;
    [self getUserData]->configuration->onElementLog = onElementLog;
    [self getUserData]->configuration->onStateChanged = onStateChanged;
    
    // Preparing pipeline
    rct_gst_init([self getUserData]);
    
    // Run it
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        rct_gst_run_loop([self getUserData]);
    });
}

// Callables
void onPlayerInit(RCTGstPlayerView *self)
{
    // Apply what has been stored in instance
    //rct_gst_set_uri([self getUserData], g_strdup([self->uri UTF8String]));
    rct_gst_set_ui_refresh_rate([self getUserData], self->refreshRate);
    
    if (self->pipelineState != NULL)
        [self setPipelineState:self->pipelineState];
    
    rct_gst_set_drawable_surface([self getUserData], [self getHandle]);
    self.onPlayerInit(@{});
}

void onPadAdded(RCTGstPlayerView *self, gchar *name)
{
    NSString *pad_name = [NSString stringWithUTF8String:name];
    self.onPadAdded(@{ @"name": pad_name });
}

void onEOS(RCTGstPlayerView *self)
{
    self.onEOS(@{});
}

void onPlayingProgress(RCTGstPlayerView *self, gint64 progress, gint64 duration) {
    dispatch_async(dispatch_get_main_queue(), ^{
        self.onPlayingProgress(@{
                             @"progress": [NSNumber numberWithInteger:progress],
                             @"duration": [NSNumber numberWithInteger:duration]
                             });
    });
}

void onBufferingProgress(RCTGstPlayerView *self, gint progress) {
  dispatch_async(dispatch_get_main_queue(), ^{
      self.onBufferingProgress(@{
      @"progress": [NSNumber numberWithInteger:progress]
      });
  });
}

void onElementError(RCTGstPlayerView *self, gchar *source, gchar *message, gchar *debug_info) {
  NSLog(@"onElementError: %s, %s, %s", source, message, debug_info);
  dispatch_async(dispatch_get_main_queue(), ^{
      self.onElementError(@{
      @"source": [NSString stringWithUTF8String:source],
      @"message": [NSString stringWithUTF8String:message],
      @"debug_info": [NSString stringWithUTF8String:debug_info]
      });
  });
}

void onElementLog(RCTGstPlayerView *self, gchar *newMessage) {
    NSString *message = [NSString stringWithUTF8String:newMessage];
    g_print("%s", newMessage);
    if (self.onElementLog)
        self.onElementLog(@{ @"message": message });
}

void onStateChanged(RCTGstPlayerView *self, GstState old_state, GstState new_state) {
    NSNumber* oldState = [NSNumber numberWithInt:old_state];
    NSNumber* newState = [NSNumber numberWithInt:new_state];
  dispatch_async(dispatch_get_main_queue(), ^{
      self.onStateChanged(@{ @"old_state": oldState, @"new_state": newState });
  });
}

-(void)removeFromSuperview {
    NSLog(@"Removing video surface : %@", self);
    if (!shareInstance) {
        [[NSNotificationCenter defaultCenter] removeObserver:self name:UIApplicationWillResignActiveNotification object:nil];
        [[NSNotificationCenter defaultCenter] removeObserver:self name:UIApplicationDidEnterBackgroundNotification object:nil];
        [[NSNotificationCenter defaultCenter] removeObserver:self name:UIApplicationDidBecomeActiveNotification object:nil];
        rct_gst_terminate([self getUserData]);
        [super removeFromSuperview];
    }
}

// Getters
- (guintptr)getHandle
{
    return (guintptr)(id)self;
}

- (RctGstUserData *)getUserData
{
    if (!self->userData) {
        g_print("Creating user data");
        self->userData = rct_gst_init_user_data();
        self->userData->configuration->owner = (__bridge void *)(self);
    }
    return self->userData;
}

- (gboolean)isReady
{
    return [self getUserData]->is_ready;
}

// Methods
- (void)setPipelineState:(int)pipelineState {
    self->pipelineState = pipelineState;
    rct_gst_set_playbin_state([self getUserData], self->pipelineState);
}

- (void)seek:(gint64)position {
    rct_gst_seek([self getUserData], position);
}

// Setters
- (void)setShareInstance:(BOOL)_shareInstance {
    shareInstance = _shareInstance;
}

@end
