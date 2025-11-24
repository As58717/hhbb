package com.example.omnicompose.call

import android.content.Context
import org.webrtc.AudioSource
import org.webrtc.AudioTrack
import org.webrtc.Camera2Enumerator
import org.webrtc.DefaultVideoDecoderFactory
import org.webrtc.DefaultVideoEncoderFactory
import org.webrtc.EglBase
import org.webrtc.PeerConnection
import org.webrtc.PeerConnectionFactory
import org.webrtc.SurfaceTextureHelper
import org.webrtc.VideoCapturer
import org.webrtc.VideoSource
import org.webrtc.VideoTrack

/**
 * Thin WebRTC bootstrap wrapper. Replace the TODO sections with your own
 * signaling (Socket.IO/MQTT/etc.) and STUN/TURN credentials.
 */
class VideoCallManager(private val context: Context) {
    private val eglBase = EglBase.create()
    private var factory: PeerConnectionFactory? = null
    private var localVideoSource: VideoSource? = null
    private var audioSource: AudioSource? = null

    fun initialize() {
        val initOptions = PeerConnectionFactory.InitializationOptions.builder(context)
            .setEnableInternalTracer(true)
            .createInitializationOptions()
        PeerConnectionFactory.initialize(initOptions)

        val encoderFactory = DefaultVideoEncoderFactory(eglBase.eglBaseContext, true, true)
        val decoderFactory = DefaultVideoDecoderFactory(eglBase.eglBaseContext)
        factory = PeerConnectionFactory.builder()
            .setVideoEncoderFactory(encoderFactory)
            .setVideoDecoderFactory(decoderFactory)
            .createPeerConnectionFactory()
    }

    fun startLocalTracks(): Pair<VideoTrack, AudioTrack> {
        val pcFactory = factory ?: error("Call manager not initialized")
        val videoCapturer = createCameraCapturer()
        val surfaceHelper = SurfaceTextureHelper.create("CaptureThread", eglBase.eglBaseContext)
        localVideoSource = pcFactory.createVideoSource(videoCapturer.isScreencast)
        videoCapturer.initialize(surfaceHelper, context, localVideoSource!!.capturerObserver)
        videoCapturer.startCapture(720, 1280, 30)
        val videoTrack = pcFactory.createVideoTrack("LOCAL_VIDEO", localVideoSource)

        audioSource = pcFactory.createAudioSource(MediaConstraintsBuilder.voice())
        val audioTrack = pcFactory.createAudioTrack("LOCAL_AUDIO", audioSource)
        return videoTrack to audioTrack
    }

    fun createPeerConnection(iceServers: List<PeerConnection.IceServer>): PeerConnection {
        val pcFactory = factory ?: error("Call manager not initialized")
        val rtcConfig = PeerConnection.RTCConfiguration(iceServers).apply {
            sdpSemantics = PeerConnection.SdpSemantics.UNIFIED_PLAN
        }
        return pcFactory.createPeerConnection(rtcConfig, SimplePeerObserver())
            ?: error("Failed to create peer connection")
    }

    fun release() {
        localVideoSource?.dispose()
        audioSource?.dispose()
        factory?.dispose()
        eglBase.release()
    }

    private fun createCameraCapturer(): VideoCapturer {
        val enumerator = Camera2Enumerator(context)
        val deviceName = enumerator.deviceNames.firstOrNull { enumerator.isFrontFacing(it) }
            ?: enumerator.deviceNames.first()
        return enumerator.createCapturer(deviceName, null)
            ?: error("No camera capturer available")
    }
}

object MediaConstraintsBuilder {
    fun voice() = org.webrtc.MediaConstraints().apply {
        mandatory.add(org.webrtc.MediaConstraints.KeyValuePair("OfferToReceiveAudio", "true"))
        optional.add(org.webrtc.MediaConstraints.KeyValuePair("DtlsSrtpKeyAgreement", "true"))
    }
}

class SimplePeerObserver : PeerConnection.Observer {
    override fun onSignalingChange(p0: PeerConnection.SignalingState?) {}
    override fun onIceConnectionChange(p0: PeerConnection.IceConnectionState?) {}
    override fun onStandardizedIceConnectionChange(newState: PeerConnection.IceConnectionState?) {}
    override fun onConnectionChange(newState: PeerConnection.PeerConnectionState?) {}
    override fun onIceConnectionReceivingChange(p0: Boolean) {}
    override fun onIceGatheringChange(p0: PeerConnection.IceGatheringState?) {}
    override fun onIceCandidate(p0: org.webrtc.IceCandidate?) {}
    override fun onIceCandidatesRemoved(p0: Array<out org.webrtc.IceCandidate>?) {}
    override fun onAddStream(p0: org.webrtc.MediaStream?) {}
    override fun onRemoveStream(p0: org.webrtc.MediaStream?) {}
    override fun onDataChannel(p0: org.webrtc.DataChannel?) {}
    override fun onRenegotiationNeeded() {}
    override fun onAddTrack(p0: org.webrtc.RtpReceiver?, p1: Array<out org.webrtc.MediaStream>?) {}
    override fun onTrack(transceiver: org.webrtc.RtpTransceiver?) {}
}
