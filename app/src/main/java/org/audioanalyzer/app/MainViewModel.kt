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
import org.audioanalyzer.core.audio.GenSignal
import org.audioanalyzer.core.audio.InputDevice
import org.audioanalyzer.core.audio.InputPreset
import org.audioanalyzer.core.audio.SpectrumWindow
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
    // RTA configuration + invalidation counter for the spectrum buffers.
    val rtaFftSize: Int = 8192,
    val rtaWindow: SpectrumWindow = SpectrumWindow.HANN,
    val rtaAvgTauSec: Double = 0.5,
    val rtaPsd: Boolean = false,
    /** Fractional-octave smoothing denominator (0 = off, 3 = 1/3 oct, ...). */
    val rtaSmoothing: Int = 12,
    /** True while the RTA tab is visible — enables the fast spectrum poll. */
    val rtaActive: Boolean = false,
    val rtaVersion: Long = 0,
    // Generator configuration.
    val outputDevices: List<InputDevice> = emptyList(),
    val genOutputDeviceId: Int = 0, // 0 = platform default
    val genSignal: GenSignal = GenSignal.PINK,
    val genFreqHz: Double = 1000.0,
    val genLevelDb: Double = -12.0,
    val genSweepF1: Double = 20.0,
    val genSweepF2: Double = 20000.0,
    val genSweepDurSec: Double = 5.0,
    val genSyncFrame: Boolean = true,
    val genErrorCode: Int? = null,
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

    /** Latest RTA traces in dB; valid for [rtaBins] bins of [rtaBinHz] each.
     *  Read after observing [MainUiState.rtaVersion]. */
    var rtaAvg = FloatArray(8192 / 2 + 1); private set
    var rtaPeak = FloatArray(8192 / 2 + 1); private set
    var rtaBins = 0; private set
    var rtaBinHz = 0.0; private set

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
                // Poll unconditionally: the snapshot also carries generator
                // status, which is valid while the input stream is stopped.
                val snap = engine.snapshot()
                if (_state.value.running) appendLogPoint(snap)
                _state.update {
                    it.copy(
                        snapshot = snap,
                        running = snap.running,
                        logVersion = it.logVersion + 1,
                    )
                }
                delay(pollMs)
            }
        }
        // Fast spectrum poll, decoupled from the 10 Hz health/log cadence:
        // ~30 fps while the RTA tab is visible and the stream runs. The
        // native read drains the capture ring itself, so RTA update rate is
        // limited only by the FFT hop (fftSize/4 samples).
        viewModelScope.launch {
            while (isActive) {
                val s = _state.value
                if (s.running && s.rtaActive) {
                    val bins = engine.readSpectrum(rtaAvg, rtaPeak, s.rtaPsd)
                    if (bins > 0) {
                        rtaBins = bins
                        rtaBinHz = (s.snapshot?.sampleRateNominal ?: 48000)
                            .toDouble() / s.rtaFftSize
                        _state.update { it.copy(rtaVersion = it.rtaVersion + 1) }
                    }
                    delay(33)
                } else {
                    delay(200)
                }
            }
        }
    }

    fun refreshDevices() {
        _state.update {
            it.copy(
                devices = DeviceCatalog.inputDevices(getApplication()),
                outputDevices = DeviceCatalog.outputDevices(getApplication()),
            )
        }
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

    // --- RTA ---

    fun setRtaConfig(
        fftSize: Int = _state.value.rtaFftSize,
        window: SpectrumWindow = _state.value.rtaWindow,
        avgTauSec: Double = _state.value.rtaAvgTauSec,
    ) {
        _state.update {
            it.copy(rtaFftSize = fftSize, rtaWindow = window, rtaAvgTauSec = avgTauSec)
        }
        val nb = fftSize / 2 + 1
        if (rtaAvg.size < nb) {
            rtaAvg = FloatArray(nb)
            rtaPeak = FloatArray(nb)
        }
        rtaBins = 0
        engine.configureSpectrum(fftSize, window, avgTauSec)
    }

    fun setRtaPsd(psd: Boolean) = _state.update { it.copy(rtaPsd = psd) }

    fun setRtaActive(active: Boolean) = _state.update { it.copy(rtaActive = active) }

    fun setRtaSmoothing(denominator: Int) =
        _state.update { it.copy(rtaSmoothing = denominator) }

    fun resetRtaPeak() = engine.resetSpectrumPeak()

    // --- Generator ---

    val genRunning: Boolean
        get() = _state.value.snapshot?.gen?.running == true

    fun startGenerator() {
        val s = _state.value
        val rc = if (s.genSignal.isSweep) {
            engine.startSweep(
                deviceId = s.genOutputDeviceId,
                exponential = s.genSignal == GenSignal.SWEEP_EXP,
                f1 = s.genSweepF1,
                f2 = s.genSweepF2,
                durationSec = s.genSweepDurSec,
                levelDb = s.genLevelDb,
                syncFrame = s.genSyncFrame,
            )
        } else {
            engine.startTone(s.genOutputDeviceId, s.genSignal, s.genFreqHz, s.genLevelDb)
        }
        _state.update { it.copy(genErrorCode = if (rc == 0) null else rc) }
    }

    fun stopGenerator() = engine.stopGenerator()

    fun setGenSignal(signal: GenSignal) = _state.update { it.copy(genSignal = signal) }

    fun setGenFrequency(hz: Double) {
        _state.update { it.copy(genFreqHz = hz) }
        if (genRunning && !_state.value.genSignal.isSweep) {
            engine.setTone(hz, _state.value.genLevelDb)
        }
    }

    fun setGenLevel(db: Double) {
        _state.update { it.copy(genLevelDb = db) }
        if (genRunning && !_state.value.genSignal.isSweep) {
            engine.setTone(_state.value.genFreqHz, db)
        }
    }

    fun setGenSweep(
        f1: Double = _state.value.genSweepF1,
        f2: Double = _state.value.genSweepF2,
        durSec: Double = _state.value.genSweepDurSec,
    ) = _state.update { it.copy(genSweepF1 = f1, genSweepF2 = f2, genSweepDurSec = durSec) }

    fun setGenSyncFrame(on: Boolean) = _state.update { it.copy(genSyncFrame = on) }

    fun selectGenOutput(id: Int) = _state.update { it.copy(genOutputDeviceId = id) }

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
        engine.stopGenerator()
        engine.stop()
    }
}
