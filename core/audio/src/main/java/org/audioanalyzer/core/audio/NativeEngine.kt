package org.audioanalyzer.core.audio

/**
 * Raw JNI surface for libaa_engine.so. Internal — use [AudioEngine].
 * Function signatures must match engine/jni_bridge.cpp exactly.
 */
internal object NativeEngine {
    init {
        System.loadLibrary("aa_engine")
    }

    external fun nativeStart(
        deviceId: Int,
        sampleRate: Int,
        channelCount: Int,
        inputPreset: Int,
    ): Int

    external fun nativeStop()

    external fun nativeSnapshot(out: DoubleArray)

    external fun nativeSnapshotSize(): Int
}
