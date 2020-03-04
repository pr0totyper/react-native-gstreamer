# UDP Video Streaming in React Native with Gstreamer
This is a fork of react-native-gstreamer project.
This fork is focused on UDP Video Streaming.

You can test it using the gstreamer command on a raspberry pi (with camera module & rpicamsrc installed):
```
gst-launch-1.0 -v rpicamsrc num-buffers=-1 ! video/x-raw,width=640,height=480,framerate=41/1 ! theoraenc ! rtptheorapay config-interval=5 ! udpsink host=IP_OF_YOUR_PHONE port=5000

```

## How to link to your project
<span style="color:red"><b>/!\ Be sure to read everything carefully : GStreamer is a  C Library. It will be necessary to finalize the linking manually.</b></span>
    
* [Linking for Android](./docs/linking_android.md)
* [Linking for iOS](./docs/linking_ios.md)

## Basic usage

```js
import React, { Component } from 'react'
import { StyleSheet, View, Button } from 'react-native'
import GstPlayer, { GstState } from 'react-native-gstreamer'

export default class App extends Component {
    render() {
        return (
            <View style={styles.container}>
                <GstPlayer
                    style={styles.videoPlayer}
                    ref="GstPlayer"
                    autoPlay={true}
                />
                <View style={styles.controlBar}>
                    <Button title="Stop" onPress={() => this.refs.GstPlayer.stop()}></Button>
                    <Button title="Pause" onPress={() => this.refs.GstPlayer.pause()}></Button>
                    <Button title="Play" onPress={() => this.refs.GstPlayer.play()}></Button>
                </View>
            </View>
        )
    }
}

const styles = StyleSheet.create({
    container: { flex: 1 },
    controlBar: {
        flexDirection: "row",
        justifyContent: "space-between"
    },
    videoPlayer: { flex: 1 }
})
```
