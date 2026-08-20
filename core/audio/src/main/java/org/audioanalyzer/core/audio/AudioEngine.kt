package org.audioanalyzer.core.audio

/**
 * Public entry point to the native audio engine.
 *
 * Threading: [start]/[stop]/[snapshot] may be called from any single thread
 * (typically a ViewModel); the native side serializes them with a mutex. The
 * real-time audio callback never blocks on these calls.
 */
class AudioEngine {
    private val scratch = DoubleArray(NativeEngine.nativeSnapshotSize())

    /**
     * Opens and starts the input stream.
     *
     * @param deviceId AudioDeviceInfo.getId(), or 0 to let the platform pick.
     * @param sampleRate Hz, or 0 for the device-native rate (recommended:
     *   avoids hidden resampling).
     * @param channelCount 0 for device-native.
     * @return 0 on success, otherwise a negative oboe::Result code.
     */
    fun start(
        deviceId: Int = 0,
        sampleRate: Int = 0,
        channelCount: Int = 0,
        inputPreset: InputPreset = InputPreset.UNPROCESSED,
    ): Int = NativeEngine.nativeStart(deviceId, sampleRate, channelCount, inputPreset.oboeValue)

    fun stop() = NativeEngine.nativeStop()

    /** Applies immediately if running, and to every later [start]. */
    fun configureSpl(weighting: Weighting, timeWeighting: TimeWeighting) =
        NativeEngine.nativeSplConfigure(weighting.nativeValue, timeWeighting.nativeValue)

    /** Clears Leq/Lmax/Lmin/LN/elapsed; the live level is unaffected. */
    fun resetSplStats() = NativeEngine.nativeSplResetStats()

    /**
     * Polls current engine state. Each call also collects one hardware
     * timestamp, so poll at a steady cadence (~10 Hz) for the clock-drift
     * estimate to converge.
     */
    fun snapshot(): EngineSnapshot {
        NativeEngine.nativeSnapshot(scratch)
        return EngineSnapshot.fromArray(scratch)
    }
}
