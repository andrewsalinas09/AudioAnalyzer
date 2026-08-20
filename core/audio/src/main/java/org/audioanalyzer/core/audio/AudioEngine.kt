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

    /** RTA configuration; applies immediately if running, and to later starts. */
    fun configureSpectrum(fftSize: Int, window: SpectrumWindow, avgTauSec: Double) =
        NativeEngine.nativeSpectrumConfigure(fftSize, window.nativeValue, avgTauSec)

    /**
     * Computes any due FFT frames and fills [avgOut]/[peakOut] with dB values
     * ([psd] selects dBFS/Hz density scaling for the average trace).
     * Returns the number of valid bins, or 0 if not running / no data yet.
     * Bin k is at frequency k * sampleRate / fftSize.
     */
    fun readSpectrum(avgOut: FloatArray, peakOut: FloatArray, psd: Boolean): Int =
        NativeEngine.nativeSpectrumRead(avgOut, peakOut, psd)

    fun resetSpectrumPeak() = NativeEngine.nativeSpectrumResetPeak()

    /**
     * Starts a continuous tone/noise on the output device (0 = default).
     * Level in dBFS (0 = full scale). Returns 0 or an oboe error code.
     */
    fun startTone(deviceId: Int, signal: GenSignal, freqHz: Double, levelDb: Double): Int =
        NativeEngine.nativeGenStartTone(deviceId, signal.nativeValue, freqHz, levelDb)

    /** Starts a one-shot sweep, optionally wrapped in the acoustic sync frame. */
    fun startSweep(
        deviceId: Int,
        exponential: Boolean,
        f1: Double,
        f2: Double,
        durationSec: Double,
        levelDb: Double,
        syncFrame: Boolean,
    ): Int = NativeEngine.nativeGenStartSweep(
        deviceId, exponential, f1, f2, durationSec, levelDb, syncFrame,
    )

    /** Click-free live update of a running tone. */
    fun setTone(freqHz: Double, levelDb: Double) = NativeEngine.nativeGenSetTone(freqHz, levelDb)

    fun stopGenerator() = NativeEngine.nativeGenStop()

    // --- IR measurement (see AudioEngine.h for the flow) ---

    fun irBeginCapture(seconds: Double): Int = NativeEngine.nativeIrBeginCapture(seconds)

    fun irAbort() = NativeEngine.nativeIrAbort()

    fun irState(): IrState = IrState.from(NativeEngine.nativeIrState())

    fun irCapturedSec(): Double = NativeEngine.nativeIrCapturedSec()

    /** Heavy: run on a worker dispatcher. */
    fun irAnalyze(f1: Double, f2: Double, durationSec: Double): Int =
        NativeEngine.nativeIrAnalyze(f1, f2, durationSec)

    fun irSummary(): IrSummary {
        val a = DoubleArray(IrSummary.SIZE)
        NativeEngine.nativeIrSummary(a)
        return IrSummary.fromArray(a)
    }

    fun irEtc(out: FloatArray): Int = NativeEngine.nativeIrEtc(out)

    fun irMag(magOut: FloatArray, gdOut: FloatArray): Int =
        NativeEngine.nativeIrMag(magOut, gdOut)

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
