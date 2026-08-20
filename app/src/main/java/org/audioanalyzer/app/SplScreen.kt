package org.audioanalyzer.app

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import org.audioanalyzer.core.audio.TimeWeighting
import org.audioanalyzer.core.audio.Weighting

@Composable
fun SplScreen(viewModel: MainViewModel) {
    val state by viewModel.state.collectAsState()
    val spl = state.snapshot?.spl
    val offset = state.cal.totalOffsetDb
    val calibrated = offset != null

    fun show(db: Double?): String =
        if (db == null || db.isNaN()) "—" else "%.1f".format(db + (offset ?: 0.0))

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("SPL Meter", style = MaterialTheme.typography.headlineSmall)

        SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
            Weighting.entries.forEachIndexed { i, w ->
                SegmentedButton(
                    selected = state.weighting == w,
                    onClick = { viewModel.setWeighting(w) },
                    shape = SegmentedButtonDefaults.itemShape(i, Weighting.entries.size),
                ) { Text(w.label) }
            }
        }
        SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
            TimeWeighting.entries.forEachIndexed { i, tw ->
                SegmentedButton(
                    selected = state.timeWeighting == tw,
                    onClick = { viewModel.setTimeWeighting(tw) },
                    shape = SegmentedButtonDefaults.itemShape(i, TimeWeighting.entries.size),
                ) { Text(tw.name.lowercase().replaceFirstChar { c -> c.uppercase() }) }
            }
        }

        // Big readout.
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(
                modifier = Modifier.fillMaxWidth().padding(16.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Text(
                    spl?.descriptor ?: "L${state.weighting.suffix}${state.timeWeighting.suffix}",
                    style = MaterialTheme.typography.titleMedium,
                )
                Text(
                    show(spl?.instantDb),
                    fontSize = 76.sp,
                    fontFamily = FontFamily.Monospace,
                )
                Text(
                    if (calibrated) "dB SPL (${state.weighting.suffix}-weighted)"
                    else "dBFS (${state.weighting.suffix}-weighted) — uncalibrated",
                    style = MaterialTheme.typography.bodyMedium,
                    color = if (calibrated) MaterialTheme.colorScheme.onSurface
                    else MaterialTheme.colorScheme.error,
                )
                if (calibrated) {
                    Text(
                        "cal: ${state.cal.selectedName ?: "manual trim"} " +
                            "(%+.1f dB)".format(offset),
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
                // Which mic is actually feeding the meter: resolve the
                // running stream's device id, falling back to the selection.
                val activeId = if (state.running) state.snapshot?.deviceId else null
                val micLabel = when {
                    activeId != null ->
                        state.devices.firstOrNull { it.id == activeId }?.label
                            ?: "device #$activeId"
                    state.selectedDeviceId == 0 -> "Platform default"
                    else -> state.devices.firstOrNull { it.id == state.selectedDeviceId }?.label
                        ?: "device #${state.selectedDeviceId}"
                }
                Text(
                    (if (state.running) "mic: " else "mic (selected): ") + micLabel,
                    style = MaterialTheme.typography.bodySmall,
                )
            }
        }

        // Statistics.
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(
                modifier = Modifier.padding(12.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                Text("Statistics", style = MaterialTheme.typography.titleMedium)
                StatRow2("Leq", show(spl?.leqDb))
                StatRow2("Lmax / Lmin", "${show(spl?.lmaxDb)} / ${show(spl?.lminDb)}")
                StatRow2("L10", show(spl?.l10Db))
                StatRow2("L50", show(spl?.l50Db))
                StatRow2("L90", show(spl?.l90Db))
                val el = spl?.elapsedSec ?: 0.0
                StatRow2("Elapsed", "%d:%02d".format(el.toInt() / 60, el.toInt() % 60))
            }
        }

        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            if (state.running) {
                Button(onClick = viewModel::stop) { Text("Stop") }
            } else {
                Button(onClick = viewModel::start) { Text("Start") }
            }
            OutlinedButton(onClick = viewModel::resetSplStats) { Text("Reset stats") }
        }

        state.startErrorCode?.let {
            Text(
                "Stream failed to open (oboe error $it)",
                color = MaterialTheme.colorScheme.error,
            )
        }
    }
}

@Composable
private fun StatRow2(label: String, value: String) {
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, style = MaterialTheme.typography.bodyMedium)
        Text(
            value,
            style = MaterialTheme.typography.bodyMedium,
            fontFamily = FontFamily.Monospace,
        )
    }
}
