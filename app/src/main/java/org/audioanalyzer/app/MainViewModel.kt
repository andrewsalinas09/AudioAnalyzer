package org.audioanalyzer.app

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.audioanalyzer.core.audio.AudioEngine
import org.audioanalyzer.core.audio.DeviceCatalog
import org.audioanalyzer.core.audio.EngineSnapshot
import org.audioanalyzer.core.audio.InputDevice
import org.audioanalyzer.core.audio.InputPreset
import org.audioanalyzer.core.audio.TimeWeighting
import org.audioanalyzer.core.audio.Weighting

data class MainUiState(
    val devices: List<InputDevice> = emptyList(),
    val selectedDeviceId: Int = 0, // 0 = platform default
    val inputPreset: InputPreset = InputPreset.UNPROCESSED,
    val unprocessedSupported: Boolean = false,
    val running: Boolean = false,
    val snapshot: EngineSnapshot? = null,
    val startErrorCode: Int? = null,
    val weighting: Weighting = Weighting.A,
    val timeWeighting: TimeWeighting = TimeWeighting.FAST,
    val cal: CalibrationRepository.CalState = CalibrationRepository.CalState(),
    /** Bumped whenever [MainViewModel.logPoints] changes (chart invalidation). */
    val logVersion: Long = 0,
)

/** One SPL log sample. Levels stay in weighted dBFS so a calibration change
 *  re-maps the whole history consistently at display time. */
data class SplLogPoint(
    val tSec: Double,
    val instantDbfs: Double,
    val leqDbfs: Double,
    val descriptor: String,
)

class MainViewModel(application: Application) : AndroidViewModel(application) {

    private val engine = AudioEngine()
    private val calRepo = CalibrationRepository(application)
    private val _state = MutableStateFlow(MainUiState())
    val state: StateFlow<MainUiState> = _state.asStateFlow()

    /**
     * SPL time log, appended at the poll cadence while the stream runs.
     * Read from the UI after observing [MainUiState.logVersion]. Bounded to
     * ~2 h at 10 Hz; the oldest points fall off.
     */
    val logPoints = ArrayDeque<SplLogPoint>()
    private val maxLogPoints = 72_000
    private var logStartRealtimeMs: Long? = null

    /** Poll cadence; the clock-drift regression assumes a steady ~10 Hz. */
    private val pollMs = 100L

    init {
        refreshDevices()
        _state.update {
            it.copy(
                unprocessedSupported = DeviceCatalog.supportsUnprocessed(application),
                cal = calRepo.load(),
            )
        }
        engine.configureSpl(_state.value.weighting, _state.value.timeWeighting)
        viewModelScope.launch {
            while (isActive) {
                if (_state.value.running) {
                    val snap = engine.snapshot()
                    appendLogPoint(snap)
                    _state.update {
                        it.copy(
                            snapshot = snap,
                            running = snap.running,
                            logVersion = it.logVersion + 1,
                        )
                    }
                }
                delay(pollMs)
            }
        }
    }

    fun refreshDevices() {
        _state.update { it.copy(devices = DeviceCatalog.inputDevices(getApplication())) }
    }

    fun selectDevice(id: Int) {
        _state.update { it.copy(selectedDeviceId = id) }
        if (_state.value.running) restart()
    }

    fun selectPreset(preset: InputPreset) {
        _state.update { it.copy(inputPreset = preset) }
        if (_state.value.running) restart()
    }

    fun start() {
        val s = _state.value
        val result = engine.start(
            deviceId = s.selectedDeviceId,
            inputPreset = s.inputPreset,
        )
        _state.update {
            it.copy(
                running = result == 0,
                startErrorCode = if (result == 0) null else result,
            )
        }
    }

    fun stop() {
        engine.stop()
        _state.update { it.copy(running = false) }
    }

    // --- SPL ---

    fun setWeighting(w: Weighting) {
        _state.update { it.copy(weighting = w) }
        engine.configureSpl(w, _state.value.timeWeighting)
    }

    fun setTimeWeighting(tw: TimeWeighting) {
        _state.update { it.copy(timeWeighting = tw) }
        engine.configureSpl(_state.value.weighting, tw)
    }

    fun resetSplStats() = engine.resetSplStats()

    // --- Calibration ---

    fun importCalibration(uri: Uri) {
        _state.update { it.copy(cal = calRepo.import(uri)) }
    }

    fun selectCalibration(name: String?) {
        _state.update { it.copy(cal = calRepo.select(name)) }
    }

    fun setManualTrim(db: Double) {
        _state.update { it.copy(cal = calRepo.setManualTrim(db)) }
    }

    fun deleteCalibration(name: String) {
        _state.update { it.copy(cal = calRepo.delete(name)) }
    }

    // --- SPL time log ---

    private fun appendLogPoint(snap: EngineSnapshot) {
        val level = snap.spl.instantDb
        if (level.isNaN()) return
        val now = android.os.SystemClock.elapsedRealtime()
        val start = logStartRealtimeMs ?: now.also { logStartRealtimeMs = it }
        logPoints.addLast(
            SplLogPoint(
                tSec = (now - start) / 1000.0,
                instantDbfs = level,
                leqDbfs = snap.spl.leqDb,
                descriptor = snap.spl.descriptor,
            ),
        )
        while (logPoints.size > maxLogPoints) logPoints.removeFirst()
    }

    /** The most recent [windowSec] seconds of the log (all of it for null). */
    fun visiblePoints(windowSec: Double?): List<SplLogPoint> {
        if (logPoints.isEmpty()) return emptyList()
        if (windowSec == null) return logPoints.toList()
        val tEnd = logPoints.last().tSec
        val tStart = tEnd - windowSec
        var i = logPoints.size - 1
        while (i > 0 && logPoints[i - 1].tSec >= tStart) i--
        return List(logPoints.size - i) { k -> logPoints[i + k] }
    }

    fun clearLog() {
        logPoints.clear()
        logStartRealtimeMs = null
        _state.update { it.copy(logVersion = it.logVersion + 1) }
    }

    /** Writes the log as CSV into the cache and returns a shareable Uri. */
    fun exportLogCsv(): android.net.Uri {
        val app = getApplication<Application>()
        val s = _state.value
        val dir = java.io.File(app.cacheDir, "exports").apply { mkdirs() }
        val stamp = java.text.SimpleDateFormat("yyyyMMdd_HHmmss", java.util.Locale.US)
            .format(java.util.Date())
        val file = java.io.File(dir, "spl_log_$stamp.csv")
        val offset = s.cal.totalOffsetDb
        file.bufferedWriter().use { w ->
            w.appendLine("# AudioAnalyzer SPL log")
            w.appendLine("# exported: $stamp")
            w.appendLine("# calibration: ${s.cal.selectedName ?: "none"}")
            w.appendLine("# spl_offset_db: ${offset ?: "none (levels are dBFS)"}")
            w.appendLine("# manual_trim_db: ${s.cal.manualTrimDb}")
            w.appendLine("elapsed_s,descriptor,level_dbfs,leq_dbfs,level_spl,leq_spl")
            for (p in logPoints) {
                val spl = offset?.let { "%.2f".format(p.instantDbfs + it) } ?: ""
                val leqSpl = offset?.let { "%.2f".format(p.leqDbfs + it) } ?: ""
                w.appendLine(
                    "%.1f,%s,%.2f,%.2f,%s,%s".format(
                        p.tSec, p.descriptor, p.instantDbfs, p.leqDbfs, spl, leqSpl,
                    ),
                )
            }
        }
        return androidx.core.content.FileProvider.getUriForFile(
            app, "org.audioanalyzer.fileprovider", file,
        )
    }

    private fun restart() {
        stop()
        start()
    }

    override fun onCleared() {
        engine.stop()
    }
}
