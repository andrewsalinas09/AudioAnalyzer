package org.audioanalyzer.app

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.horizontalScroll
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
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp

@Composable
fun CalibrationScreen(viewModel: MainViewModel) {
    val state by viewModel.state.collectAsState()
    val cal = state.cal

    val importLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument(),
    ) { uri -> uri?.let(viewModel::importCalibration) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Calibration", style = MaterialTheme.typography.headlineSmall)

        Button(onClick = { importLauncher.launch(arrayOf("*/*")) }) {
            Text("Import calibration file…")
        }
        cal.importError?.let {
            Text(it, color = MaterialTheme.colorScheme.error)
        }

        if (cal.files.isNotEmpty()) {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(12.dp)) {
                    Text("Files", style = MaterialTheme.typography.titleMedium)
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        RadioButton(
                            selected = cal.selectedName == null,
                            onClick = { viewModel.selectCalibration(null) },
                        )
                        Text("None (manual trim only)")
                    }
                    cal.files.forEach { name ->
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            RadioButton(
                                selected = cal.selectedName == name,
                                onClick = { viewModel.selectCalibration(name) },
                            )
                            Text(name, modifier = Modifier.weight(1f))
                            TextButton(onClick = { viewModel.deleteCalibration(name) }) {
                                Text("Delete")
                            }
                        }
                    }
                }
            }
        }

        cal.calibration?.let { c ->
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(
                    modifier = Modifier.padding(12.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    Text("Parsed: ${cal.selectedName}", style = MaterialTheme.typography.titleMedium)
                    c.header.sensFactorDb?.let { KV("Sens Factor", "%.2f dB".format(it)) }
                    c.header.analogGainDb?.let { KV("Analog gain (AGain)", "%.1f dB".format(it)) }
                    c.header.serialNumber?.let { KV("Serial", it) }
                    c.header.refFrequencyHz?.let { KV("Reference frequency", "%.0f Hz".format(it)) }
                    c.header.refValueDb?.let { KV("Sensitivity @ ref", "%.1f dB".format(it)) }
                    KV("Data points", "${c.points.size}")
                    KV("Range", "%.1f Hz – %.0f Hz".format(c.minFreqHz, c.maxFreqHz))
                    KV("Phase data", if (c.hasPhase) "yes" else "no")
                    cal.fileOffsetDb?.let {
                        KV("SPL offset from file", "dBFS %+.2f dB → dB SPL".format(it))
                    } ?: Text(
                        "No usable sensitivity in header — set the manual trim below.",
                        color = MaterialTheme.colorScheme.error,
                        style = MaterialTheme.typography.bodySmall,
                    )

                    Text(
                        "Raw file start (compare with the parsed values above):",
                        style = MaterialTheme.typography.bodySmall,
                    )
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .horizontalScroll(rememberScrollState()),
                    ) {
                        c.previewLines.forEach { line ->
                            Text(
                                line,
                                fontFamily = FontFamily.Monospace,
                                style = MaterialTheme.typography.bodySmall,
                                maxLines = 1,
                            )
                        }
                    }
                }
            }
        }

        Card(modifier = Modifier.fillMaxWidth()) {
            Column(
                modifier = Modifier.padding(12.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Text("Manual SPL trim", style = MaterialTheme.typography.titleMedium)
                Text(
                    "Added to the file offset (or used alone for the built-in mic). " +
                        "Match the reading against a reference SLM or a 94 dB calibrator.",
                    style = MaterialTheme.typography.bodySmall,
                )
                var trimText by rememberSaveable(cal.manualTrimDb) {
                    mutableStateOf(
                        if (cal.manualTrimDb == 0.0) "0" else "%.1f".format(cal.manualTrimDb),
                    )
                }
                Row(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    OutlinedButton(onClick = { viewModel.setManualTrim(cal.manualTrimDb - 0.5) }) {
                        Text("−0.5")
                    }
                    OutlinedTextField(
                        value = trimText,
                        onValueChange = { text ->
                            trimText = text
                            text.toDoubleOrNull()?.let(viewModel::setManualTrim)
                        },
                        label = { Text("dB") },
                        singleLine = true,
                        modifier = Modifier.weight(1f),
                    )
                    OutlinedButton(onClick = { viewModel.setManualTrim(cal.manualTrimDb + 0.5) }) {
                        Text("+0.5")
                    }
                }
                KV(
                    "Total SPL offset",
                    cal.totalOffsetDb?.let { "%+.2f dB".format(it) } ?: "none (uncalibrated)",
                )
            }
        }
    }
}

@Composable
private fun KV(label: String, value: String) {
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, style = MaterialTheme.typography.bodyMedium)
        Text(value, style = MaterialTheme.typography.bodyMedium)
    }
}
