package org.audioanalyzer.app

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import org.audioanalyzer.core.audio.EngineSnapshot
import org.audioanalyzer.core.audio.InputPreset

@Composable
fun AudioHealthScreen(viewModel: MainViewModel) {
    val state by viewModel.state.collectAsState()
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Audio Health", style = MaterialTheme.typography.headlineSmall)

        DeviceSelector(state, viewModel)
        PresetSelector(state, viewModel)

        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            if (state.running) {
                Button(onClick = viewModel::stop) { Text("Stop") }
            } else {
                Button(onClick = viewModel::start) { Text("Start") }
            }
            OutlinedButton(onClick = viewModel::refreshDevices) { Text("Rescan devices") }
        }

        state.startErrorCode?.let {
            Text(
                "Stream failed to open (oboe error $it)",
                color = MaterialTheme.colorScheme.error,
            )
        }
        // Some OEMs (e.g. Samsung) grant the Unprocessed preset per-stream
        // without declaring the global property, so only warn when the
        // running stream actually failed to get it.
        val streamNotUnprocessed = state.running &&
            state.snapshot?.let { !it.isUnprocessed } == true &&
            state.inputPreset == InputPreset.UNPROCESSED
        if (streamNotUnprocessed) {
            Text(
                "The stream was NOT granted the Unprocessed preset — the " +
                    "platform may be applying AGC/filtering to this input.",
                color = MaterialTheme.colorScheme.error,
                style = MaterialTheme.typography.bodySmall,
            )
        } else if (!state.unprocessedSupported && !state.running) {
            Text(
                "This device does not declare global UNPROCESSED support; " +
                    "after starting, check the Input preset row — a per-stream " +
                    "grant (common on Samsung) is what actually counts.",
                style = MaterialTheme.typography.bodySmall,
            )
        }

        state.snapshot?.takeIf { state.running }?.let { snap ->
            LevelCard(snap)
            StreamCard(snap)
            ClockCard(snap)
            CallbackCard(snap)
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun DeviceSelector(state: MainUiState, viewModel: MainViewModel) {
    var expanded by remember { mutableStateOf(false) }
    val selectedLabel = state.devices.firstOrNull { it.id == state.selectedDeviceId }?.label
        ?: "Platform default"
    ExposedDropdownMenuBox(expanded = expanded, onExpandedChange = { expanded = it }) {
        OutlinedTextField(
            value = selectedLabel,
            onValueChange = {},
            readOnly = true,
            label = { Text("Input device") },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded) },
            modifier = Modifier.menuAnchor().fillMaxWidth(),
        )
        ExposedDropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            DropdownMenuItem(
                text = { Text("Platform default") },
                onClick = {
                    viewModel.selectDevice(0)
                    expanded = false
                },
            )
            state.devices.forEach { dev ->
                DropdownMenuItem(
                    text = { Text("${dev.label}  [rates: ${dev.sampleRates.joinToString()}]") },
                    onClick = {
                        viewModel.selectDevice(dev.id)
                        expanded = false
                    },
                )
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun PresetSelector(state: MainUiState, viewModel: MainViewModel) {
    var expanded by remember { mutableStateOf(false) }
    ExposedDropdownMenuBox(expanded = expanded, onExpandedChange = { expanded = it }) {
        OutlinedTextField(
            value = state.inputPreset.label,
            onValueChange = {},
            readOnly = true,
            label = { Text("Input preset") },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded) },
            modifier = Modifier.menuAnchor().fillMaxWidth(),
        )
        ExposedDropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            InputPreset.entries.forEach { preset ->
                DropdownMenuItem(
                    text = { Text(preset.label) },
                    onClick = {
                        viewModel.selectPreset(preset)
                        expanded = false
                    },
                )
            }
        }
    }
}

@Composable
private fun LevelCard(snap: EngineSnapshot) {
    StatCard("Level") {
        LevelBar("Ch 0", snap.rmsDbfsCh0, snap.peakDbfsCh0)
        if (!snap.rmsDbfsCh1.isNaN()) {
            LevelBar("Ch 1", snap.rmsDbfsCh1, snap.peakDbfsCh1)
        }
    }
}

@Composable
private fun LevelBar(label: String, rmsDbfs: Double, peakDbfs: Double) {
    val range = 90.0 // display floor at -90 dBFS
    val fraction = ((rmsDbfs + range) / range).coerceIn(0.0, 1.0)
    Column {
        Text(
            "$label   RMS %.1f dBFS   peak %.1f dBFS".format(rmsDbfs, peakDbfs),
            style = MaterialTheme.typography.bodyMedium,
        )
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(10.dp)
                .background(MaterialTheme.colorScheme.surfaceVariant, RoundedCornerShape(5.dp)),
        ) {
            Box(
                modifier = Modifier
                    .fillMaxWidth(fraction.toFloat())
                    .fillMaxHeight()
                    .background(
                        if (peakDbfs > -1.0) Color(0xFFE57373) else Color(0xFF4FC3F7),
                        RoundedCornerShape(5.dp),
                    ),
            )
        }
    }
}

@Composable
private fun StreamCard(snap: EngineSnapshot) {
    StatCard("Stream") {
        StatRow("Audio API", if (snap.audioApi == 2) "AAudio" else "OpenSL ES")
        StatRow("Sample rate (nominal)", "${snap.sampleRateNominal} Hz")
        StatRow("Channels", "${snap.channelCount}")
        StatRow("Frames per burst", "${snap.framesPerBurst}")
        StatRow("Buffer size / capacity", "${snap.bufferSizeFrames} / ${snap.bufferCapacityFrames}")
        StatRow("Sharing", if (snap.sharingMode == 0) "Exclusive" else "Shared")
        StatRow(
            "Performance mode",
            when (snap.performanceMode) {
                12 -> "Low latency"
                11 -> "Power saving"
                else -> "None"
            },
        )
        StatRow("MMAP", if (snap.mmapUsed == 1) "Yes" else if (snap.mmapUsed == 0) "No" else "Unknown")
        StatRow(
            "Input preset",
            InputPreset.entries.firstOrNull { it.oboeValue == snap.inputPresetActual }?.label
                ?: "Value ${snap.inputPresetActual}",
            warn = !snap.isUnprocessed,
        )
        StatRow("Device id", "${snap.deviceId}")
    }
}

@Composable
private fun ClockCard(snap: EngineSnapshot) {
    StatCard("Sample clock vs system clock") {
        if (snap.measuredSampleRateHz.isNaN()) {
            Text(
                "Collecting timestamps… (${snap.timestampCount})",
                style = MaterialTheme.typography.bodyMedium,
            )
        } else {
            StatRow("Measured rate", "%.3f Hz".format(snap.measuredSampleRateHz))
            StatRow(
                "Clock drift",
                "%+.1f ppm".format(snap.clockDriftPpm),
                warn = kotlin.math.abs(snap.clockDriftPpm) > 100,
            )
            StatRow("Timestamps", "${snap.timestampCount}")
        }
    }
}

@Composable
private fun CallbackCard(snap: EngineSnapshot) {
    StatCard("Callback timing") {
        StatRow("Callbacks", "${snap.callbackCount}")
        StatRow("Frames read", "${snap.framesRead}")
        StatRow("Interval mean", "%.2f ms".format(snap.cbIntervalMeanMs))
        StatRow("Interval min / max", "%.2f / %.2f ms".format(snap.cbIntervalMinMs, snap.cbIntervalMaxMs))
        StatRow("Interval p99", "%.2f ms".format(snap.cbIntervalP99Ms))
        StatRow("XRuns", if (snap.xrunCount >= 0) "${snap.xrunCount}" else "n/a", warn = snap.xrunCount > 0)
    }
}

@Composable
private fun StatCard(title: String, content: @Composable () -> Unit) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            content()
        }
    }
}

@Composable
private fun StatRow(label: String, value: String, warn: Boolean = false) {
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, style = MaterialTheme.typography.bodyMedium)
        Text(
            value,
            style = MaterialTheme.typography.bodyMedium,
            color = if (warn) MaterialTheme.colorScheme.error else Color.Unspecified,
        )
    }
}
