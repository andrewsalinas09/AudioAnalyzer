package org.audioanalyzer.app

import android.app.Application
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

data class AudioHealthUiState(
    val devices: List<InputDevice> = emptyList(),
    val selectedDeviceId: Int = 0, // 0 = platform default
    val inputPreset: InputPreset = InputPreset.UNPROCESSED,
    val unprocessedSupported: Boolean = false,
    val running: Boolean = false,
    val snapshot: EngineSnapshot? = null,
    val startErrorCode: Int? = null,
)

class AudioHealthViewModel(application: Application) : AndroidViewModel(application) {

    private val engine = AudioEngine()
    private val _state = MutableStateFlow(AudioHealthUiState())
    val state: StateFlow<AudioHealthUiState> = _state.asStateFlow()

    /** Poll cadence; the clock-drift regression assumes a steady ~10 Hz. */
    private val pollMs = 100L

    init {
        refreshDevices()
        _state.update {
            it.copy(unprocessedSupported = DeviceCatalog.supportsUnprocessed(application))
        }
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

    private fun restart() {
        stop()
        start()
    }

    override fun onCleared() {
        engine.stop()
    }
}
