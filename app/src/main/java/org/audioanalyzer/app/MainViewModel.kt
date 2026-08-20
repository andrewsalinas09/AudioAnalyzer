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
    // IR measurement.
    val irSweepF1: Double = 20.0,
    val irSweepF2: Double = 20000.0,
    val irSweepDurSec: Double = 5.0,
    /** Repetitions to coherently average (local playback only). */
    val irRepeat: Int = 1,
    val irRepNow: Int = 0,
    val irPhase: IrPhase = IrPhase.IDLE,
    val irProgress: Float = 0f,
    val irError: String? = null,
    val irResult: org.audioanalyzer.core.audio.IrSummary? = null,
    val irVersion: Long = 0,
)

enum class IrPhase { IDLE, MEASURING, ANALYZING, DONE, FAILED }

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
                val appended = snap.running && appendLogPoint(snap)
                _state.update {
                    it.copy(
                        snapshot = snap,
                        running = snap.running,
                        // Only bump when a point landed: an unconditional bump
                        // makes the state differ every tick, forcing 10 Hz
                        // recompositions even when idle (which killed dropdown
                        // popups). With it gated, StateFlow's equality check
                        // suppresses idle emissions entirely.
                        logVersion = if (appended) it.logVersion + 1 else it.logVersion,
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

    // --- IR measurement ---

    /** ETC (dB) and magnitude/GD traces; read after observing irVersion. */
    val irEtc = FloatArray(2048)
    var irEtcValid = false; private set
    val irMagDb = FloatArray(16384 / 2 + 1)
    val irGdMs = FloatArray(16384 / 2 + 1)
    var irMagBins = 0; private set

    fun setIrSweep(
        f1: Double = _state.value.irSweepF1,
        f2: Double = _state.value.irSweepF2,
        durSec: Double = _state.value.irSweepDurSec,
    ) = _state.update { it.copy(irSweepF1 = f1, irSweepF2 = f2, irSweepDurSec = durSec) }

    /**
     * Runs an IR measurement. With [playLocally] the sweep is played from
     * this device (full duplex, one-phone measurement). Without it the
     * device only listens: the capture window includes arming slack so
     * another device can play a sync-framed sweep with the SAME band and
     * duration — the sync frame does the alignment.
     */
    fun startIrMeasurement(playLocally: Boolean = true) {
        val s = _state.value
        if (s.irPhase == IrPhase.MEASURING || s.irPhase == IrPhase.ANALYZING) return
        if (!s.running) {
            start()
            if (!_state.value.running) {
                _state.update { it.copy(irPhase = IrPhase.FAILED, irError = "input stream failed") }
                return
            }
        }
        // Frame: chirp + guard + sweep + guard + chirp; capture adds start
        // slack (generous when waiting for a remote emitter) and reverb tail.
        val frameSec = 0.1 + 0.25 + s.irSweepDurSec + 0.25 + 0.1
        val captureSec = (if (playLocally) 1.0 else 8.0) + frameSec + 1.5
        if (engine.irBeginCapture(captureSec) != 0) {
            _state.update { it.copy(irPhase = IrPhase.FAILED, irError = "capture failed to start") }
            return
        }
        if (playLocally) {
            val rc = engine.startSweep(
                deviceId = s.genOutputDeviceId,
                exponential = true,
                f1 = s.irSweepF1,
                f2 = s.irSweepF2,
                durationSec = s.irSweepDurSec,
                levelDb = s.genLevelDb,
                syncFrame = true,
            )
            if (rc != 0) {
                engine.irAbort()
                _state.update { it.copy(irPhase = IrPhase.FAILED, irError = "output failed (oboe $rc)") }
                return
            }
        }
        engine.irResetAverage()
        _state.update { it.copy(irPhase = IrPhase.MEASURING, irError = null, irProgress = 0f) }

        // Listen-only can't coordinate repetitions with a remote emitter.
        val reps = if (playLocally) s.irRepeat else 1

        irJob = viewModelScope.launch {
            for (rep in 1..reps) {
                _state.update { it.copy(irRepNow = rep) }
                if (rep > 1) {
                    // Re-arm capture and playback for the next repetition.
                    delay(400)
                    if (engine.irBeginCapture(captureSec) != 0 ||
                        engine.startSweep(
                            deviceId = _state.value.genOutputDeviceId,
                            exponential = true,
                            f1 = _state.value.irSweepF1,
                            f2 = _state.value.irSweepF2,
                            durationSec = _state.value.irSweepDurSec,
                            levelDb = _state.value.genLevelDb,
                            syncFrame = true,
                        ) != 0
                    ) {
                        _state.update { it.copy(irPhase = IrPhase.FAILED, irError = "rep $rep failed to start") }
                        return@launch
                    }
                }
                while (isActive && engine.irState() == org.audioanalyzer.core.audio.IrState.CAPTURING) {
                    _state.update {
                        it.copy(
                            irProgress = ((rep - 1) +
                                (engine.irCapturedSec() / captureSec).toFloat()) / reps,
                        )
                    }
                    delay(100)
                }
                if (engine.irState() != org.audioanalyzer.core.audio.IrState.CAPTURED) {
                    _state.update { it.copy(irPhase = IrPhase.FAILED, irError = "capture interrupted") }
                    return@launch
                }
                _state.update { it.copy(irPhase = IrPhase.ANALYZING) }
                val result = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Default) {
                    engine.irAnalyze(_state.value.irSweepF1, _state.value.irSweepF2,
                        _state.value.irSweepDurSec)
                }
                if (result != 0) {
                    _state.update {
                        it.copy(
                            irPhase = IrPhase.FAILED,
                            irError = if (result == -1) {
                                "sync frame not detected — increase level or reduce distance"
                            } else "analysis failed ($result)",
                        )
                    }
                    return@launch
                }
                // Publish the running average after every repetition.
                irEtcValid = engine.irEtc(irEtc) > 0
                irMagBins = engine.irMag(irMagDb, irGdMs)
                _state.update {
                    it.copy(
                        irPhase = if (rep == reps) IrPhase.DONE else IrPhase.MEASURING,
                        irResult = engine.irSummary(),
                        irVersion = it.irVersion + 1,
                    )
                }
            }
        }
    }

    private var irJob: kotlinx.coroutines.Job? = null

    fun setIrRepeat(n: Int) = _state.update { it.copy(irRepeat = n) }

    fun abortIrMeasurement() {
        irJob?.cancel()
        engine.irAbort()
        engine.stopGenerator()
        _state.update { it.copy(irPhase = IrPhase.IDLE, irProgress = 0f, irRepNow = 0) }
    }

    /** Frequency response of the (averaged) IR as CSV. Null before a result. */
    fun buildIrCsv(): String? {
        val res = _state.value.irResult ?: return null
        if (irMagBins == 0) return null
        val s = _state.value
        fun ms(v: Double) = if (v.isNaN()) "n/a" else "%.1f".format(v * 1000)
        return buildString {
            appendLine("# AudioAnalyzer impulse-response measurement")
            appendLine("# exported: ${timestamp()}")
            appendLine("# sweep: log ${s.irSweepF1.toInt()}-${s.irSweepF2.toInt()} Hz, " +
                "${s.irSweepDurSec} s, level ${s.genLevelDb} dBFS")
            appendLine("# averages: ${res.avgCount}, drift_ppm: %.1f".format(res.driftPpm))
            appendLine("# rt60_t20_ms: ${ms(res.t20Sec)}, rt60_t30_ms: ${ms(res.t30Sec)}, " +
                "edt_ms: ${ms(res.edtSec)}")
            appendLine("# c50_db: %.1f, c80_db: %.1f".format(res.c50Db, res.c80Db))
            appendLine("# sync_quality: %.2f / %.2f".format(res.preambleQuality, res.postambleQuality))
            appendLine("# magnitude is uncalibrated relative dB (windowed IR)")
            appendLine("freq_hz,mag_db,group_delay_ms")
            for (i in 0 until irMagBins) {
                appendLine("%.3f,%.2f,%.3f".format(i * res.magBinHz, irMagDb[i], irGdMs[i]))
            }
        }
    }

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

    /** Returns true if a point was appended. */
    private fun appendLogPoint(snap: EngineSnapshot): Boolean {
        val level = snap.spl.instantDb
        if (level.isNaN()) return false
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
        return true
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

    private fun timestamp(): String =
        java.text.SimpleDateFormat("yyyyMMdd_HHmmss", java.util.Locale.US)
            .format(java.util.Date())

    /** The SPL log as CSV text (metadata header + rows). */
    fun buildLogCsv(): String {
        val s = _state.value
        val offset = s.cal.totalOffsetDb
        return buildString {
            appendLine("# AudioAnalyzer SPL log")
            appendLine("# exported: ${timestamp()}")
            appendLine("# calibration: ${s.cal.selectedName ?: "none"}")
            appendLine("# spl_offset_db: ${offset ?: "none (levels are dBFS)"}")
            appendLine("# manual_trim_db: ${s.cal.manualTrimDb}")
            appendLine("elapsed_s,descriptor,level_dbfs,leq_dbfs,level_spl,leq_spl")
            for (p in logPoints) {
                val spl = offset?.let { "%.2f".format(p.instantDbfs + it) } ?: ""
                val leqSpl = offset?.let { "%.2f".format(p.leqDbfs + it) } ?: ""
                appendLine(
                    "%.1f,%s,%.2f,%.2f,%s,%s".format(
                        p.tSec, p.descriptor, p.instantDbfs, p.leqDbfs, spl, leqSpl,
                    ),
                )
            }
        }
    }

    /**
     * The current RTA traces as CSV: raw per-bin dB plus display values with
     * the per-bin calibration correction applied. Unsmoothed — smoothing is
     * a display choice that analysis tools can redo. Null if no spectrum yet.
     */
    fun buildRtaCsv(): String? {
        val bins = rtaBins
        if (bins == 0) return null
        val s = _state.value
        val avg = rtaAvg.copyOf(bins)
        val peak = rtaPeak.copyOf(bins)
        val corr = RtaMath.calCorrection(
            s.cal.calibration, s.cal.totalOffsetDb, bins, rtaBinHz,
        )
        val unit = if (s.rtaPsd) "dB/Hz" else "dB"
        return buildString {
            appendLine("# AudioAnalyzer RTA spectrum")
            appendLine("# exported: ${timestamp()}")
            appendLine("# fft_size: ${s.rtaFftSize}, window: ${s.rtaWindow.label}, " +
                "avg_tau_s: ${s.rtaAvgTauSec}, scaling: ${if (s.rtaPsd) "PSD" else "amplitude"}")
            appendLine("# bin_hz: $rtaBinHz")
            appendLine("# calibration: ${s.cal.selectedName ?: "none"}, " +
                "spl_offset_db: ${s.cal.totalOffsetDb ?: "none"}")
            appendLine("# corrected = raw + spl_offset - mic_cal_gain(freq)")
            appendLine("freq_hz,avg_raw_$unit,peak_raw_db,avg_corrected,peak_corrected")
            for (i in 0 until bins) {
                appendLine(
                    "%.3f,%.2f,%.2f,%.2f,%.2f".format(
                        i * rtaBinHz, avg[i], peak[i],
                        avg[i] + corr[i], peak[i] + corr[i],
                    ),
                )
            }
        }
    }

    /** Writes [content] into the cache and returns a shareable Uri. */
    fun shareableCsv(prefix: String, content: String): android.net.Uri {
        val app = getApplication<Application>()
        val dir = java.io.File(app.cacheDir, "exports").apply { mkdirs() }
        val file = java.io.File(dir, "${prefix}_${timestamp()}.csv")
        file.writeText(content)
        return androidx.core.content.FileProvider.getUriForFile(
            app, "org.audioanalyzer.fileprovider", file,
        )
    }

    /** Writes [content] to a user-picked document (Save-to-Files flow). */
    fun writeCsvTo(uri: android.net.Uri, content: String) = writeBytesTo(uri, content.toByteArray())

    fun writeBytesTo(uri: android.net.Uri, bytes: ByteArray) {
        getApplication<Application>().contentResolver.openOutputStream(uri)?.use {
            it.write(bytes)
        }
    }

    /**
     * The sync-framed measurement sweep as a playable 16-bit/48 kHz WAV —
     * play it from ANY device (PC, AVR, second phone) while this or another
     * phone runs "Listen only" with matching band/duration settings.
     */
    fun buildSweepWav(): ByteArray {
        val s = _state.value
        val samples = engine.renderSweep(
            exponential = true,
            f1 = s.irSweepF1,
            f2 = s.irSweepF2,
            durationSec = s.irSweepDurSec,
            levelDb = s.genLevelDb,
            syncFrame = true,
            sampleRate = 48000.0,
        )
        return WavWriter.pcm16(samples, 48000)
    }

    /** The measured (averaged) IR as a 32-bit float WAV, or null if none. */
    fun buildIrWav(): ByteArray? {
        val res = _state.value.irResult ?: return null
        if (res.irSamples == 0) return null
        val buf = FloatArray(res.irSamples)
        val n = engine.irGet(buf)
        if (n == 0) return null
        return WavWriter.float32(if (n == buf.size) buf else buf.copyOf(n), res.fs.toInt())
    }

    fun exportLogCsv(): android.net.Uri = shareableCsv("spl_log", buildLogCsv())

    private fun restart() {
        stop()
        start()
    }

    override fun onCleared() {
        engine.stopGenerator()
        engine.stop()
    }
}
