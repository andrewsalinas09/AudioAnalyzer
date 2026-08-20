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
)

class MainViewModel(application: Application) : AndroidViewModel(application) {

    private val engine = AudioEngine()
    private val calRepo = CalibrationRepository(application)
    private val _state = MutableStateFlow(MainUiState())
    val state: StateFlow<MainUiState> = _state.asStateFlow()

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
                    _state.update { it.copy(snapshot = snap, running = snap.running) }
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

    private fun restart() {
        stop()
        start()
    }

    override fun onCleared() {
        engine.stop()
    }
}
