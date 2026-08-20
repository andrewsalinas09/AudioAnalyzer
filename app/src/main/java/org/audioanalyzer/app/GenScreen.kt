package org.audioanalyzer.app

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import org.audioanalyzer.core.audio.GenSignal

@Composable
fun GenScreen(viewModel: MainViewModel) {
    val state by viewModel.state.collectAsState()
    val gen = state.snapshot?.gen
    val running = gen?.running == true

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Signal generator", style = MaterialTheme.typography.headlineSmall)

        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            GenSignal.entries.forEach { sig ->
                FilterChip(
                    selected = state.genSignal == sig,
                    onClick = { viewModel.setGenSignal(sig) },
                    label = { Text(sig.label) },
                )
            }
        }

        // Level (applies to everything).
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(12.dp)) {
                Text(
                    "Level  %.1f dBFS".format(state.genLevelDb),
                    style = MaterialTheme.typography.titleMedium,
                )
                Slider(
                    value = state.genLevelDb.toFloat(),
                    onValueChange = { viewModel.setGenLevel(it.toDouble()) },
                    valueRange = -60f..0f,
                )
            }
        }

        if (state.genSignal == GenSignal.SINE) {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(
                    modifier = Modifier.padding(12.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Text("Frequency", style = MaterialTheme.typography.titleMedium)
                    var freqText by rememberSaveable(state.genFreqHz) {
                        mutableStateOf(
                            if (state.genFreqHz == state.genFreqHz.toLong().toDouble()) {
                                state.genFreqHz.toLong().toString()
                            } else "%.1f".format(state.genFreqHz),
                        )
                    }
                    OutlinedTextField(
                        value = freqText,
                        onValueChange = { t ->
                            freqText = t
                            t.toDoubleOrNull()?.takeIf { it in 1.0..24000.0 }
                                ?.let(viewModel::setGenFrequency)
                        },
                        label = { Text("Hz") },
                        singleLine = true,
                    )
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        listOf(100.0, 440.0, 1000.0, 10000.0).forEach { f ->
                            FilterChip(
                                selected = state.genFreqHz == f,
                                onClick = { viewModel.setGenFrequency(f) },
                                label = {
                                    Text(if (f >= 1000) "${(f / 1000).toInt()} kHz" else "${f.toInt()} Hz")
                                },
                            )
                        }
                    }
                }
            }
        }

        if (state.genSignal.isSweep) {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(
                    modifier = Modifier.padding(12.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Text("Sweep", style = MaterialTheme.typography.titleMedium)
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        var f1Text by rememberSaveable { mutableStateOf(state.genSweepF1.toInt().toString()) }
                        var f2Text by rememberSaveable { mutableStateOf(state.genSweepF2.toInt().toString()) }
                        OutlinedTextField(
                            value = f1Text,
                            onValueChange = { t ->
                                f1Text = t
                                t.toDoubleOrNull()?.takeIf { it >= 1.0 }
                                    ?.let { viewModel.setGenSweep(f1 = it) }
                            },
                            label = { Text("From Hz") },
                            singleLine = true,
                            modifier = Modifier.weight(1f),
                        )
                        OutlinedTextField(
                            value = f2Text,
                            onValueChange = { t ->
                                f2Text = t
                                t.toDoubleOrNull()?.takeIf { it <= 24000.0 }
                                    ?.let { viewModel.setGenSweep(f2 = it) }
                            },
                            label = { Text("To Hz") },
                            singleLine = true,
                            modifier = Modifier.weight(1f),
                        )
                    }
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        listOf(2.0, 5.0, 10.0).forEach { d ->
                            FilterChip(
                                selected = state.genSweepDurSec == d,
                                onClick = { viewModel.setGenSweep(durSec = d) },
                                label = { Text("${d.toInt()} s") },
                            )
                        }
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Switch(
                            checked = state.genSyncFrame,
                            onCheckedChange = viewModel::setGenSyncFrame,
                        )
                        Text(
                            "  Sync frame (chirp markers for cross-device alignment)",
                            style = MaterialTheme.typography.bodyMedium,
                        )
                    }
                }
            }
        }

        OutputSelector(state, viewModel)

        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            if (running) {
                Button(onClick = viewModel::stopGenerator) { Text("Stop") }
            } else {
                Button(onClick = viewModel::startGenerator) { Text("Play") }
            }
        }

        if (running && gen != null && gen.durationSec > 0) {
            LinearProgressIndicator(
                progress = { (gen.positionSec / gen.durationSec).toFloat().coerceIn(0f, 1f) },
                modifier = Modifier.fillMaxWidth(),
            )
            Text(
                "%.1f / %.1f s   (%s)".format(
                    gen.positionSec, gen.durationSec,
                    gen.signal?.label ?: "",
                ),
                style = MaterialTheme.typography.bodySmall,
            )
        }

        state.genErrorCode?.let {
            Text(
                "Output stream failed (oboe error $it)",
                color = MaterialTheme.colorScheme.error,
            )
        }

        Text(
            "Tip: the input analyzer keeps running while the generator plays — " +
                "watch pink noise on the RTA, or capture the sync-framed sweep " +
                "from another device across the room.",
            style = MaterialTheme.typography.bodySmall,
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun OutputSelector(state: MainUiState, viewModel: MainViewModel) {
    var expanded by remember { mutableStateOf(false) }
    val label = state.outputDevices.firstOrNull { it.id == state.genOutputDeviceId }?.label
        ?: "Platform default"
    ExposedDropdownMenuBox(expanded = expanded, onExpandedChange = { expanded = it }) {
        OutlinedTextField(
            value = label,
            onValueChange = {},
            readOnly = true,
            label = { Text("Output device") },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded) },
            modifier = Modifier.menuAnchor().fillMaxWidth(),
        )
        ExposedDropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            DropdownMenuItem(
                text = { Text("Platform default") },
                onClick = {
                    viewModel.selectGenOutput(0)
                    expanded = false
                },
            )
            state.outputDevices.forEach { dev ->
                DropdownMenuItem(
                    text = { Text(dev.label) },
                    onClick = {
                        viewModel.selectGenOutput(dev.id)
                        expanded = false
                    },
                )
            }
        }
    }
}
