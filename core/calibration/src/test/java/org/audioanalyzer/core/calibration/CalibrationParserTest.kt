package org.audioanalyzer.core.calibration

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Parses the real calibration files shipped in src/test/resources/calibration
 * (UMIK-2, OmniMic V2, iMM-6C — one physical mic each, actual vendor output).
 */
class CalibrationParserTest {

    private fun load(name: String): CalibrationFile {
        val text = requireNotNull(javaClass.getResourceAsStream("/calibration/$name")) {
            "missing test resource $name"
        }.bufferedReader().readText()
        return CalibrationParser.parse(text)
    }

    @Test
    fun `umik2 header and points`() {
        val cal = load("umik2_8105623.txt")
        assertEquals(-13.47, cal.header.sensFactorDb!!, 1e-9)
        assertEquals(18.0, cal.header.analogGainDb!!, 1e-9)
        assertEquals("8105623", cal.header.serialNumber)
        assertNull(cal.header.refFrequencyHz)
        assertFalse(cal.hasPhase)
        assertTrue("expected dense data", cal.points.size > 100)
        assertEquals(10.054, cal.minFreqHz, 1e-3)
    }

    @Test
    fun `umik2 90deg variant parses despite comment line`() {
        // This file has a second quoted line ("Auto-generated 90-degree
        // calibration file") that must not clobber the parsed header.
        val cal = load("umik2_8105623_90deg.txt")
        assertEquals("8105623", cal.header.serialNumber)
        assertEquals(-13.47, cal.header.sensFactorDb!!, 1e-9)
        assertEquals(18.0, cal.header.analogGainDb!!, 1e-9)
        assertTrue(cal.points.size > 100)
    }

    @Test
    fun `omnimic has phase column`() {
        val cal = load("omnimic2_2080581.omm")
        assertEquals(-5.687, cal.header.sensFactorDb!!, 1e-9)
        assertEquals("2080581", cal.header.serialNumber)
        assertNull(cal.header.analogGainDb)
        assertTrue(cal.hasPhase)
        assertNotNull(cal.phaseDegAt(1000.0))
        assertEquals(4.6758, cal.minFreqHz, 1e-4)
        // First point's phase from the file.
        assertEquals(18.34, cal.points.first().phaseDeg!!, 1e-6)
    }

    @Test
    fun `imm6c dayton star header`() {
        val cal = load("imm6c_cmm26181.txt")
        assertEquals(1000.0, cal.header.refFrequencyHz!!, 1e-9)
        assertEquals(-36.0, cal.header.refValueDb!!, 1e-9)
        assertNull(cal.header.sensFactorDb)
        assertFalse(cal.hasPhase)
        assertEquals(20.0, cal.minFreqHz, 1e-9)
        assertEquals(0.2, cal.points.first().gainDb, 1e-9)
    }

    @Test
    fun `points sorted and interpolation clamps at edges`() {
        val cal = load("umik2_8105623.txt")
        assertTrue(cal.points.zipWithNext().all { (a, b) -> a.freqHz <= b.freqHz })
        assertEquals(cal.points.first().gainDb, cal.gainDbAt(1.0), 1e-9)
        assertEquals(cal.points.last().gainDb, cal.gainDbAt(1e6), 1e-9)
    }

    @Test
    fun `interpolation is bounded by neighbors`() {
        val cal = CalibrationParser.parse(
            """
            "Sens Factor =-10.0dB, SERNO: TEST"
            100.0 -1.0
            200.0 1.0
            """.trimIndent(),
        )
        val mid = cal.gainDbAt(141.42)  // geometric mean of 100 and 200
        assertEquals(0.0, mid, 1e-2)
        assertTrue(cal.gainDbAt(150.0) > -1.0 && cal.gainDbAt(150.0) < 1.0)
    }

    @Test
    fun `preview lines keep raw text for the settings UI`() {
        val cal = load("omnimic2_2080581.omm")
        assertTrue(cal.previewLines.first().startsWith("\"Sens Factor"))
        assertTrue(cal.previewLines.size in 2..6)
    }
}
