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

    external fun nativeSplConfigure(weighting: Int, timeWeighting: Int)

    external fun nativeSplResetStats()

    external fun nativeSpectrumConfigure(fftSize: Int, window: Int, avgTauSec: Double)

    external fun nativeSpectrumRead(avgOut: FloatArray, peakOut: FloatArray, psd: Boolean): Int

    external fun nativeSpectrumResetPeak()

    external fun nativeGenStartTone(deviceId: Int, kind: Int, freqHz: Double, levelDb: Double): Int

    external fun nativeGenStartSweep(
        deviceId: Int,
        exponential: Boolean,
        f1: Double,
        f2: Double,
        durationSec: Double,
        levelDb: Double,
        syncFrame: Boolean,
    ): Int

    external fun nativeGenSetTone(freqHz: Double, levelDb: Double)

    external fun nativeGenStop()

    external fun nativeIrBeginCapture(seconds: Double): Int

    external fun nativeIrAbort()

    external fun nativeIrState(): Int

    external fun nativeIrCapturedSec(): Double

    external fun nativeIrResetAverage()

    external fun nativeIrAnalyze(f1: Double, f2: Double, durationSec: Double): Int

    external fun nativeIrSummary(out: DoubleArray)

    external fun nativeIrEtc(out: FloatArray): Int

    external fun nativeIrMag(magOut: FloatArray, gdOut: FloatArray): Int
}
