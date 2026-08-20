package org.audioanalyzer.app

import org.audioanalyzer.core.calibration.CalibrationFile
import kotlin.math.log10
import kotlin.math.pow

/** Display-side spectrum math: fractional-octave smoothing and calibration. */
object RtaMath {

    /**
     * Fractional-octave smoothing (1/[denominator] octave) of a dB spectrum:
     * each bin becomes the power average over [f/r, f*r], r = 2^(1/(2*den)).
     * O(n) via a prefix sum. Bin 0 (DC) is passed through.
     */
    fun smooth(db: FloatArray, bins: Int, denominator: Int): FloatArray {
        if (denominator <= 0 || bins < 3) return db.copyOf(bins)
        val power = DoubleArray(bins)
        for (i in 0 until bins) power[i] = 10.0.pow(db[i] / 10.0)
        val prefix = DoubleArray(bins + 1)
        for (i in 0 until bins) prefix[i + 1] = prefix[i] + power[i]

        val r = 2.0.pow(1.0 / (2.0 * denominator))
        val out = FloatArray(bins)
        out[0] = db[0]
        for (i in 1 until bins) {
            var lo = (i / r).toInt()
            var hi = (i * r).toInt() + 1
            if (lo < 1) lo = 1
            if (hi > bins) hi = bins
            if (lo >= hi) { out[i] = db[i]; continue }
            val mean = (prefix[hi] - prefix[lo]) / (hi - lo)
            out[i] = (10.0 * log10(mean.coerceAtLeast(1e-24))).toFloat()
        }
        return out
    }

    /**
     * Per-bin display correction in dB: subtracts the microphone's relative
     * response from the calibration file and adds the absolute SPL offset.
     * Zero-filled when uncalibrated (pure dBFS display).
     */
    fun calCorrection(
        cal: CalibrationFile?,
        totalOffsetDb: Double?,
        bins: Int,
        binHz: Double,
    ): FloatArray {
        val out = FloatArray(bins)
        val offset = totalOffsetDb ?: 0.0
        for (i in 0 until bins) {
            val g = cal?.gainDbAt((i * binHz).coerceAtLeast(1.0)) ?: 0.0
            out[i] = (offset - g).toFloat()
        }
        return out
    }
}
