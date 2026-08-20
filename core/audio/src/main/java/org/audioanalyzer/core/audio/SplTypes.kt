package org.audioanalyzer.core.audio

/** Values match aa::dsp::Weighting in weighting.h. */
enum class Weighting(val nativeValue: Int, val label: String, val suffix: String) {
    Z(0, "Z (flat)", "Z"),
    A(1, "A", "A"),
    C(2, "C", "C"),
}

/** Values match aa::dsp::WindowType in window.h. */
enum class SpectrumWindow(val nativeValue: Int, val label: String) {
    RECTANGULAR(0, "Rect"),
    HANN(1, "Hann"),
    FLATTOP(2, "Flat-top"),
}

/** Values match aa::dsp::TimeWeighting in spl.h. */
enum class TimeWeighting(val nativeValue: Int, val label: String, val suffix: String) {
    FAST(0, "Fast (125 ms)", "F"),
    SLOW(1, "Slow (1 s)", "S"),
    IMPULSE(2, "Impulse (35 ms / 1.5 s)", "I"),
}

/**
 * SPL statistics decoded from the snapshot, in weighted dBFS (channel 0).
 * Absolute SPL = value + calibration offset. NaN = not yet defined.
 */
data class SplStats(
    val weighting: Weighting,
    val timeWeighting: TimeWeighting,
    val instantDb: Double,
    val leqDb: Double,
    val lmaxDb: Double,
    val lminDb: Double,
    val l10Db: Double,
    val l50Db: Double,
    val l90Db: Double,
    val elapsedSec: Double,
) {
    /** e.g. "LAF" / "LCS" / "LZI" — standard level descriptor. */
    val descriptor: String get() = "L${weighting.suffix}${timeWeighting.suffix}"
}
