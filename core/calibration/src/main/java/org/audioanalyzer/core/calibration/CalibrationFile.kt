package org.audioanalyzer.core.calibration

import kotlin.math.ln

/** One calibration data point. [phaseDeg] is present only in files that carry phase (OmniMic). */
data class CalPoint(
    val freqHz: Double,
    val gainDb: Double,
    val phaseDeg: Double? = null,
)

/**
 * Parsed header metadata. The three vendors use different header dialects
 * with different sensitivity semantics — see docs/formats/calibration-files.md.
 * [raw] preserves the original line so the UI can always show it verbatim.
 */
data class CalibrationHeader(
    val raw: String? = null,
    /** miniDSP/OmniMic style: `Sens Factor =-13.47dB`. */
    val sensFactorDb: Double? = null,
    /** UMIK style: `AGain =18dB`. */
    val analogGainDb: Double? = null,
    /** `SERNO: 8105623`. */
    val serialNumber: String? = null,
    /** Dayton style `*1000Hz -36.0`: the reference frequency. */
    val refFrequencyHz: Double? = null,
    /** Dayton style: the sensitivity value at [refFrequencyHz]. */
    val refValueDb: Double? = null,
)

/**
 * A parsed microphone calibration file. [previewLines] holds the first raw
 * lines of the file for display in settings, so users can compare dialects
 * against what the app actually parsed.
 */
data class CalibrationFile(
    val header: CalibrationHeader,
    val points: List<CalPoint>,
    val previewLines: List<String>,
) {
    val hasPhase: Boolean = points.isNotEmpty() && points.all { it.phaseDeg != null }
    val minFreqHz: Double get() = points.first().freqHz
    val maxFreqHz: Double get() = points.last().freqHz

    /**
     * Correction gain at [freqHz] in dB, interpolated linearly in log-frequency.
     * Clamped to the endpoint values outside the calibrated range.
     */
    fun gainDbAt(freqHz: Double): Double = interpolate(freqHz) { it.gainDb }

    /** Phase in degrees at [freqHz], or null if the file has no phase data. */
    fun phaseDegAt(freqHz: Double): Double? =
        if (hasPhase) interpolate(freqHz) { it.phaseDeg!! } else null

    private inline fun interpolate(freqHz: Double, value: (CalPoint) -> Double): Double {
        require(points.isNotEmpty()) { "no calibration points" }
        if (freqHz <= points.first().freqHz) return value(points.first())
        if (freqHz >= points.last().freqHz) return value(points.last())
        // Binary search for the bracketing pair.
        var lo = 0
        var hi = points.size - 1
        while (hi - lo > 1) {
            val mid = (lo + hi) / 2
            if (points[mid].freqHz <= freqHz) lo = mid else hi = mid
        }
        val a = points[lo]
        val b = points[hi]
        val t = (ln(freqHz) - ln(a.freqHz)) / (ln(b.freqHz) - ln(a.freqHz))
        return value(a) + t * (value(b) - value(a))
    }
}
