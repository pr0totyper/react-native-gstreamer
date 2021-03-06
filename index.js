import React from 'react'
import {
    requireNativeComponent,
    View,
    UIManager,
    findNodeHandle,
    StyleSheet,
    Platform
} from 'react-native'

const PropTypes = require('prop-types')

export const GstState = {
    VOID_PENDING: 0,
    NULL: 1,
    READY: 2,
    PAUSED: 3,
    PLAYING: 4
}

export class GstPlayer extends React.Component {

    currentGstState = undefined
    isPlayerReady = false

    constructor(props, context) {
        super(props, context)
    }

    componentDidMount() {
        this.playerHandle = findNodeHandle(this.playerViewRef)
        this.setViewReady()
    }

    componentDidUpdate(prevProps, prevState) {
        if (prevProps.autoPlay !== this.props.autoPlay && this.props.autoPlay)
            this.play()
    }

    // Callbacks
    onPlayerInit() {
        this.isPlayerReady = true

        if (this.props.onPlayerInit)
            this.props.onPlayerInit(this)
    }

    onPadAdded(_message) {
        const { name } = _message.nativeEvent

        if (this.props.onPadAdded)
            this.props.onPadAdded(name, this)
    }

    onStateChanged(_message) {
        const { old_state, new_state } = _message.nativeEvent
        this.currentGstState = new_state

        if (this.props.onStateChanged)
            this.props.onStateChanged(old_state, new_state, this)
    }

    onVolumeChanged(_message) {
        const audioLevelObject = Object.keys(_message.nativeEvent).filter(key => key !== "target").reduce((obj, key) => {
            obj[key] = _message.nativeEvent[key];
            return obj;
        }, {})

        const audioLevelArray = Object.values(audioLevelObject)

        if (this.props.onVolumeChanged)
            this.props.onVolumeChanged(audioLevelArray, this)
    }

    onUriChanged(_message) {
        const { new_uri } = _message.nativeEvent

        if (this.props.autoPlay)
            this.play()

        if (this.props.onUriChanged)
            this.props.onUriChanged(new_uri, this)
    }

    onBufferingProgress(_message) {
        const { progress } = _message.nativeEvent

        if (this.props.onBufferingProgress)
            this.props.onBufferingProgress(progress, this)
    }

    onPlayingProgress(_message) {
        const { progress, duration } = _message.nativeEvent

        if (this.props.onPlayingProgress)
            this.props.onPlayingProgress(progress, duration, this)
    }

    onEOS() {
        if (!this.props.loop)
            this.stop()
        else
            this.seek(0)

        if (this.props.onEOS)
            this.props.onEOS(this)
    }

    onElementError(_message) {
        const { source, message, debug_info } = _message.nativeEvent

        if (this.props.onElementError)
            this.props.onElementError(source, message, debug_info, this)
    }

    onElementLog(_message) {
        const { message } = _message.nativeEvent

        if (this.props.onElementLog)
            this.props.onElementLog(message, this)
    }

    // Methods
    setGstState(state) {
        UIManager.dispatchViewManagerCommand(
            this.playerHandle,
            UIManager.getViewManagerConfig("RCTGstPlayer").Commands.setState,
            [state]
        )
    }

    setViewReady() {
        if (Platform.OS === "ios") {
            UIManager.dispatchViewManagerCommand(
                this.playerHandle,
                UIManager.getViewManagerConfig("RCTGstPlayer").Commands.setViewReady,
                null
            )
        }
    }

    seek(position) {
        UIManager.dispatchViewManagerCommand(
            this.playerHandle,
            UIManager.getViewManagerConfig("RCTGstPlayer").Commands.seek,
            [position]
        )
    }

    // Player state shortcuts
    play() {
        this.setGstState(GstState.PLAYING)
    }

    pause() {
        this.setGstState(GstState.PAUSED)
    }

    stop() {
        this.setGstState(GstState.READY)
    }

    destroy() {
        UIManager.dispatchViewManagerCommand(
            this.playerHandle,
            UIManager.getViewManagerConfig("RCTGstPlayer").Commands.destroy,
            null
        )
    }

    isReady() {
        return this.isPlayerReady
    }

    render() {
        return (
            <View
                style={[styles.playerContainer, this.props.containerStyle]}
                pointerEvents='box-none'
            >
                <RCTGstPlayer
                    uri={this.props.uri !== undefined ? this.props.uri : ""}
                    shareInstance={this.props.shareInstance !== undefined ? this.props.shareInstance : false}
                    uiRefreshRate={this.props.uiRefreshRate !== undefined ? this.props.uiRefreshRate : 100}
                    volume={this.props.volume !== undefined ? this.props.volume : 1.0}

                    onPlayerInit={this.onPlayerInit.bind(this)}
                    onPadAdded={this.onPadAdded.bind(this)}
                    onStateChanged={this.onStateChanged.bind(this)}
                    onVolumeChanged={this.onVolumeChanged.bind(this)}
                    onUriChanged={this.onUriChanged.bind(this)}
                    onBufferingProgress={this.onBufferingProgress.bind(this)}
                    onPlayingProgress={this.onPlayingProgress.bind(this)}
                    onEOS={this.onEOS.bind(this)}
                    onElementError={this.onElementError.bind(this)}
                    onElementLog={this.onElementLog.bind(this)}

                    ref={(playerView) => this.playerViewRef = playerView}

                    style={[styles.player, this.props.playerStyle]}
                />
            </View>
        )
    }
}

GstPlayer.propTypes = {

    // Props
    uri: PropTypes.string,
    autoPlay: PropTypes.bool,
    loop: PropTypes.bool,
    uiRefreshRate: PropTypes.number,
    shareInstance: PropTypes.bool,
    volume: PropTypes.number,
    overlayOpacity: PropTypes.number,
    overlayFadeInSpeed: PropTypes.number,
    overlayFadeOutSpeed: PropTypes.number,

    // Events callbacks
    onPlayerInit: PropTypes.func,
    onPadAdded: PropTypes.func,
    onStateChanged: PropTypes.func,
    onVolumeChanged: PropTypes.func,
    onUriChanged: PropTypes.func,
    onBufferingProgress: PropTypes.func,
    onPlayingProgress: PropTypes.func,
    onEOS: PropTypes.func,
    onElementError: PropTypes.func,
    onElementLog: PropTypes.func,

    // Methods
    setGstState: PropTypes.func,
    setViewReady: PropTypes.func,
    play: PropTypes.func,
    pause: PropTypes.func,
    stop: PropTypes.func,
    destroy: PropTypes.func,

    // Helper methods
    createDrawableSurface: PropTypes.func,
    destroyDrawableSurface: PropTypes.func,

    ...View.propTypes
}

const styles = StyleSheet.create({
    playerContainer: {
        flex: 1,
        backgroundColor: '#000'
    },
    player: {
        flex: 1
    },
    overlay: {
        backgroundColor: '#000000',
        position: 'absolute',
        top: 0, left: 0, right: 0, bottom: 0
    }
})

const RCTGstPlayer = requireNativeComponent('RCTGstPlayer', GstPlayer, {
    nativeOnly: { onChange: true }
})
