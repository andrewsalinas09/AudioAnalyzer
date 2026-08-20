package org.audioanalyzer.core.audio

/** Native IR-measurement states (AudioEngine.h). */
enum class IrState(val nativeValue: Int) {
    IDLE(0),
    CAPTURING(1),
    CAPTURED(2),
    ANALYZING(3),
    DONE(4),
    SYNC_NOT_FOUND(-1),
    ERROR(-2),
    ;

    companion object {
        fun from(v: Int): IrState = entries.firstOrNull { it.nativeValue == v } ?: ERROR
    }
}

/** Mirror of the native irSummary layout (AudioEngine.h kIrSummarySize). */
data class IrSummary(
    val fs: Double,
    val capturedSec: Double,
    val peakSample: Double,
    val peakDb: Double,
    val edtSec: Double,
    val t20Sec: Double,
    val t30Sec: Double,
    val c50Db: Double,
    val c80Db: Double,
    val driftPpm: Double,
    val preambleQuality: Double,
    val postambleQuality: Double,
    val irSamples: Int,
    val magBins: Int,
    val magBinHz: Double,
    /** Number of coherently averaged repetitions in this result. */
    val avgCount: Int,
) {
    companion object {
        const val SIZE = 16

        fun fromArray(a: DoubleArray): IrSummary = IrSummary(
            fs = a[0], capturedSec = a[1], peakSample = a[2], peakDb = a[3],
            edtSec = a[4], t20Sec = a[5], t30Sec = a[6], c50Db = a[7],
            c80Db = a[8], driftPpm = a[9], preambleQuality = a[10],
            postambleQuality = a[11], irSamples = a[12].toInt(),
            magBins = a[13].toInt(), magBinHz = a[14], avgCount = a[15].toInt(),
        )
    }
}
