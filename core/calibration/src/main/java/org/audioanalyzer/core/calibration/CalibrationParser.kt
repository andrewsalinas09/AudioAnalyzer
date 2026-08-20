package org.audioanalyzer.core.calibration

/**
 * Parser for microphone calibration files.
 *
 * Known dialects (see docs/formats/calibration-files.md for full examples):
 *  - miniDSP UMIK-1/2:  `"Sens Factor =-13.47dB, AGain =18dB, SERNO: 8105623"`
 *                       then `freq gain` pairs.
 *  - OmniMic V2 (.omm): same quoted header style (no AGain),
 *                       then `freq gain phase` triples.
 *  - Dayton iMM-6/6C:   `*1000Hz	-36.0` star header,
 *                       then `freq gain` pairs.
 *
 * The parser is deliberately lenient: any line that is not a header and not
 * 2–3 numeric columns is skipped. Points are sorted by frequency.
 */
object CalibrationParser {

    private val sensFactorRegex = Regex("""Sens\s*Factor\s*=\s*(-?[\d.]+)\s*dB""", RegexOption.IGNORE_CASE)
    private val aGainRegex = Regex("""AGain\s*=\s*(-?[\d.]+)\s*dB""", RegexOption.IGNORE_CASE)
    private val serNoRegex = Regex("""SERNO:\s*([^\s",]+)""", RegexOption.IGNORE_CASE)
    private val daytonRegex = Regex("""^\*\s*([\d.]+)\s*Hz\s+(-?[\d.]+)""", RegexOption.IGNORE_CASE)

    fun parse(text: String): CalibrationFile {
        val lines = text.lines()
        val preview = lines.filter { it.isNotBlank() }.take(6)

        var header = CalibrationHeader()
        var headerSeen = false
        val points = ArrayList<CalPoint>(lines.size)

        for (line in lines) {
            val trimmed = line.trim()
            if (trimmed.isEmpty()) continue

            // Only the first quoted/star line is the header. Some files carry
            // additional quoted lines (e.g. UMIK 90° files add
            // "Auto-generated 90-degree calibration file") — those are
            // comments and must not overwrite the parsed header.
            if (trimmed.startsWith("\"")) {
                if (!headerSeen) {
                    header = parseQuotedHeader(trimmed)
                    headerSeen = true
                }
                continue
            }
            if (trimmed.startsWith("*")) {
                if (!headerSeen) {
                    header = parseDaytonHeader(trimmed) ?: CalibrationHeader(raw = trimmed)
                    headerSeen = true
                }
                continue
            }

            val cols = trimmed.split(Regex("""[\s,]+"""))
            if (cols.size !in 2..3) continue
            val freq = cols[0].toDoubleOrNull() ?: continue
            val gain = cols[1].toDoubleOrNull() ?: continue
            val phase = if (cols.size == 3) cols[2].toDoubleOrNull() else null
            points += CalPoint(freq, gain, phase)
        }

        require(points.isNotEmpty()) { "calibration file contains no data points" }
        points.sortBy { it.freqHz }
        return CalibrationFile(header, points, preview)
    }

    private fun parseQuotedHeader(line: String): CalibrationHeader = CalibrationHeader(
        raw = line,
        sensFactorDb = sensFactorRegex.find(line)?.groupValues?.get(1)?.toDoubleOrNull(),
        analogGainDb = aGainRegex.find(line)?.groupValues?.get(1)?.toDoubleOrNull(),
        serialNumber = serNoRegex.find(line)?.groupValues?.get(1),
    )

    private fun parseDaytonHeader(line: String): CalibrationHeader? {
        val m = daytonRegex.find(line) ?: return null
        return CalibrationHeader(
            raw = line,
            refFrequencyHz = m.groupValues[1].toDoubleOrNull(),
            refValueDb = m.groupValues[2].toDoubleOrNull(),
        )
    }
}
