package org.audioanalyzer.app

import android.content.Context
import android.net.Uri
import org.audioanalyzer.core.calibration.CalibrationFile
import org.audioanalyzer.core.calibration.CalibrationParser
import java.io.File

/**
 * Stores imported calibration files in app-private storage and remembers the
 * selection plus the manual SPL trim.
 *
 * SPL offset semantics (ADR-0006, to be validated against a calibrator —
 * validation entry 005):
 *  - miniDSP/OmniMic `Sens Factor` S: the mic reads S dBFS at 94 dB SPL,
 *    so offset = 94 - S.
 *  - Dayton `*1000Hz V`: treated the same way (offset = 94 - V) for the
 *    USB iMM-6C; marked provisional in the UI.
 * Total offset = file offset (if any) + manual trim.
 */
class CalibrationRepository(context: Context) {

    private val dir = File(context.filesDir, "calibration").apply { mkdirs() }
    private val prefs = context.getSharedPreferences("calibration", Context.MODE_PRIVATE)
    private val resolver = context.contentResolver

    data class CalState(
        val files: List<String> = emptyList(),
        val selectedName: String? = null,
        val calibration: CalibrationFile? = null,
        /** dB to add to dBFS for absolute SPL, from the file header. */
        val fileOffsetDb: Double? = null,
        /** User trim in dB, added on top (or standalone with no file). */
        val manualTrimDb: Double = 0.0,
        val importError: String? = null,
    ) {
        /** Null means "uncalibrated": show dBFS. */
        val totalOffsetDb: Double? =
            if (fileOffsetDb != null) fileOffsetDb + manualTrimDb
            else if (manualTrimDb != 0.0) manualTrimDb
            else null
    }

    fun load(): CalState {
        val files = dir.listFiles()?.map { it.name }?.sorted() ?: emptyList()
        val selected = prefs.getString(KEY_SELECTED, null)?.takeIf { it in files }
        val trim = prefs.getFloat(KEY_TRIM, 0f).toDouble()
        val cal = selected?.let { runCatching { parseFile(it) }.getOrNull() }
        return CalState(
            files = files,
            selectedName = if (cal != null) selected else null,
            calibration = cal,
            fileOffsetDb = cal?.let { offsetFromHeader(it) },
            manualTrimDb = trim,
        )
    }

    /** Copies the document into storage and selects it. */
    fun import(uri: Uri): CalState {
        val name = queryDisplayName(uri) ?: "imported_${System.currentTimeMillis()}.txt"
        return try {
            val text = resolver.openInputStream(uri)?.bufferedReader()?.readText()
                ?: error("could not open file")
            CalibrationParser.parse(text) // validate before persisting
            File(dir, name).writeText(text)
            select(name)
        } catch (e: Exception) {
            load().copy(importError = "Could not import $name: ${e.message}")
        }
    }

    fun select(name: String?): CalState {
        prefs.edit().putString(KEY_SELECTED, name).apply()
        return load()
    }

    fun setManualTrim(db: Double): CalState {
        prefs.edit().putFloat(KEY_TRIM, db.toFloat()).apply()
        return load()
    }

    fun delete(name: String): CalState {
        File(dir, name).delete()
        if (prefs.getString(KEY_SELECTED, null) == name) {
            prefs.edit().remove(KEY_SELECTED).apply()
        }
        return load()
    }

    private fun parseFile(name: String): CalibrationFile =
        CalibrationParser.parse(File(dir, name).readText())

    private fun offsetFromHeader(cal: CalibrationFile): Double? {
        cal.header.sensFactorDb?.let { return 94.0 - it }
        cal.header.refValueDb?.let { return 94.0 - it }
        return null
    }

    private fun queryDisplayName(uri: Uri): String? =
        resolver.query(uri, null, null, null, null)?.use { c ->
            val idx = c.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
            if (idx >= 0 && c.moveToFirst()) c.getString(idx) else null
        }

    private companion object {
        const val KEY_SELECTED = "selected"
        const val KEY_TRIM = "manual_trim_db"
    }
}
