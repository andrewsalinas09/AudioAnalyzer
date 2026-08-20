package org.audioanalyzer.core.audio

/**
 * Decoded engine state. Field order/indices must stay in sync with the
 * SnapshotField enum in engine/AudioEngine.h (the C++ side is the source of
 * truth for indices; this file is the source of truth for meaning/units).
 */
data class EngineSnapshot(
    val running: Boolean,
    /** 0 unspecified, 1 OpenSL ES, 2 AAudio. */
    val audioApi: Int,
    /** Nominal (requested/granted) sample rate in Hz. */
    val sampleRateNominal: Int,
    val framesPerBurst: Int,
    val bufferSizeFrames: Int,
    val bufferCapacityFrames: Int,
    val channelCount: Int,
    /** oboe::PerformanceMode value: 10 None, 11 PowerSaving, 12 LowLatency. */
    val performanceMode: Int,
    /** oboe::SharingMode value: 0 Exclusive, 1 Shared. */
    val sharingMode: Int,
    val deviceId: Int,
    /** 1 = MMAP path in use, 0 = legacy path, -1 = unknown. */
    val mmapUsed: Int,
    /** Underrun/overrun count reported by the stream; -1 if unsupported. */
    val xrunCount: Int,
    val framesRead: Long,
    val callbackCount: Long,
    val cbIntervalMeanMs: Double,
    val cbIntervalMinMs: Double,
    val cbIntervalMaxMs: Double,
    val cbIntervalP99Ms: Double,
    /** ADC clock rate measured against CLOCK_MONOTONIC; NaN until converged. */
    val measuredSampleRateHz: Double,
    /** (measured/nominal - 1) * 1e6; NaN until converged. */
    val clockDriftPpm: Double,
    val timestampCount: Int,
    val rmsDbfsCh0: Double,
    val peakDbfsCh0: Double,
    /** NaN when the stream is mono. */
    val rmsDbfsCh1: Double,
    val peakDbfsCh1: Double,
    /** oboe::Result as int, 0 = OK. */
    val lastErrorCode: Int,
    /** oboe::InputPreset value actually granted, see [InputPreset]. */
    val inputPresetActual: Int,
    /** SPL engine statistics (channel 0, weighted dBFS). */
    val spl: SplStats,
    /** Generator status (valid regardless of the input stream's state). */
    val gen: GenStatus,
) {
    val isUnprocessed: Boolean
        get() = inputPresetActual == InputPreset.UNPROCESSED.oboeValue

    companion object {
        internal fun fromArray(a: DoubleArray): EngineSnapshot = EngineSnapshot(
            running = a[0] != 0.0,
            audioApi = a[1].toInt(),
            sampleRateNominal = a[2].toInt(),
            framesPerBurst = a[3].toInt(),
            bufferSizeFrames = a[4].toInt(),
            bufferCapacityFrames = a[5].toInt(),
            channelCount = a[6].toInt(),
            performanceMode = a[7].toInt(),
            sharingMode = a[8].toInt(),
            deviceId = a[9].toInt(),
            mmapUsed = a[10].toInt(),
            xrunCount = a[11].toInt(),
            framesRead = a[12].toLong(),
            callbackCount = a[13].toLong(),
            cbIntervalMeanMs = a[14],
            cbIntervalMinMs = a[15],
            cbIntervalMaxMs = a[16],
            cbIntervalP99Ms = a[17],
            measuredSampleRateHz = a[18],
            clockDriftPpm = a[19],
            timestampCount = a[20].toInt(),
            rmsDbfsCh0 = a[21],
            peakDbfsCh0 = a[22],
            rmsDbfsCh1 = a[23],
            peakDbfsCh1 = a[24],
            lastErrorCode = a[25].toInt(),
            inputPresetActual = a[26].toInt(),
            spl = SplStats(
                weighting = Weighting.entries.first { it.nativeValue == a[27].toInt() },
                timeWeighting = TimeWeighting.entries.first { it.nativeValue == a[28].toInt() },
                instantDb = a[29],
                leqDb = a[30],
                lmaxDb = a[31],
                lminDb = a[32],
                l10Db = a[33],
                l50Db = a[34],
                l90Db = a[35],
                elapsedSec = a[36],
            ),
            gen = GenStatus(
                running = a[37] != 0.0,
                signal = GenSignal.entries.firstOrNull { it.nativeValue == a[38].toInt() },
                positionSec = a[39],
                durationSec = a[40],
            ),
        )
    }
}

/** Values match oboe::InputPreset (which matches AAudio's). */
enum class InputPreset(val oboeValue: Int, val label: String) {
    GENERIC(1, "Generic"),
    CAMCORDER(5, "Camcorder"),
    VOICE_RECOGNITION(6, "Voice recognition"),
    VOICE_COMMUNICATION(7, "Voice communication"),
    UNPROCESSED(9, "Unprocessed"),
    VOICE_PERFORMANCE(10, "Voice performance"),
}
