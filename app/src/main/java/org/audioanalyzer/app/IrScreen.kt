package org.audioanalyzer.app

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.FilterChip
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.ceil
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.roundToInt

@Composable
fun IrScreen(viewModel: MainViewModel) {
    val state by viewModel.state.collectAsState()
    var view by rememberSaveable { mutableStateOf(0) }  // 0 ETC, 1 Mag, 2 GD
    val busy = state.irPhase == IrPhase.MEASURING || state.irPhase == IrPhase.ANALYZING

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Impulse response", style = MaterialTheme.typography.headlineSmall)

        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            listOf(2.0, 5.0, 10.0).forEach { d ->
                FilterChip(
                    selected = state.irSweepDurSec == d,
                    onClick = { viewModel.setIrSweep(durSec = d) },
                    label = { Text("${d.toInt()} s sweep") },
                )
            }
            FilterChip(
                selected = state.irSweepF1 >= 50.0,
                onClick = {
                    if (state.irSweepF1 >= 50.0) viewModel.setIrSweep(f1 = 20.0, f2 = 20000.0)
                    else viewModel.setIrSweep(f1 = 50.0, f2 = 16000.0)
                },
                label = {
                    Text("${state.irSweepF1.toInt()}–${(state.irSweepF2 / 1000).toInt()}k Hz")
                },
            )
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            listOf(1, 2, 4, 8).forEach { n ->
                FilterChip(
                    selected = state.irRepeat == n,
                    onClick = { viewModel.setIrRepeat(n) },
                    label = { Text(if (n == 1) "Single" else "Avg ×$n") },
                )
            }
        }
        Text(
            "Level %.1f dBFS (set on the Gen tab) — output: %s".format(
                state.genLevelDb,
                state.outputDevices.firstOrNull { it.id == state.genOutputDeviceId }?.label
                    ?: "Platform default",
            ),
            style = MaterialTheme.typography.bodySmall,
        )

        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Button(
                onClick = { viewModel.startIrMeasurement(playLocally = true) },
                enabled = !busy,
            ) {
                Text(if (busy) "Measuring…" else "Measure")
            }
            OutlinedButton(
                onClick = { viewModel.startIrMeasurement(playLocally = false) },
                enabled = !busy,
            ) { Text("Listen only") }
            if (busy) {
                OutlinedButton(onClick = viewModel::abortIrMeasurement) { Text("Abort") }
            }
        }
        Text(
            "Measure: this phone plays the sync-framed sweep and records it — " +
                "a complete one-device measurement.\n" +
                "Listen only: this phone just records for a while; play a " +
                "sync-framed Sweep (log) with the SAME band and duration from " +
                "another device's Gen tab — or the exported sweep WAV from any " +
                "player. The chirp markers align and drift-correct the capture.",
            style = MaterialTheme.typography.bodySmall,
        )

        var pendingWav by remember { mutableStateOf<ByteArray?>(null) }
        val wavLauncher = androidx.activity.compose.rememberLauncherForActivityResult(
            androidx.activity.result.contract.ActivityResultContracts.CreateDocument("audio/wav"),
        ) { uri ->
            val bytes = pendingWav
            if (uri != null && bytes != null) viewModel.writeBytesTo(uri, bytes)
            pendingWav = null
        }
        OutlinedButton(
            onClick = {
                pendingWav = viewModel.buildSweepWav()
                wavLauncher.launch(
                    "sync_sweep_%d-%d_%ds.wav".format(
                        state.irSweepF1.toInt(), state.irSweepF2.toInt(),
                        state.irSweepDurSec.toInt(),
                    ),
                )
            },
            enabled = !busy,
        ) { Text("Save sweep WAV (for external playback)") }
        if (busy) {
            LinearProgressIndicator(
                progress = { state.irProgress },
                modifier = Modifier.fillMaxWidth(),
            )
            Text(
                (if (state.irRepeat > 1) "Rep ${state.irRepNow}/${state.irRepeat} — " else "") +
                    if (state.irPhase == IrPhase.ANALYZING) "analyzing…" else "capturing sweep…",
                style = MaterialTheme.typography.bodySmall,
            )
        }
        state.irError?.let {
            Text(it, color = MaterialTheme.colorScheme.error)
        }

        val res = state.irResult
        if (state.irPhase == IrPhase.DONE && res != null) {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(
                    modifier = Modifier.padding(12.dp),
                    verticalArrangement = Arrangement.spacedBy(4.dp),
                ) {
                    Text("Results", style = MaterialTheme.typography.titleMedium)
                    fun t(v: Double) = if (v.isNaN()) "—" else "%.0f ms".format(v * 1000)
                    IrRow("RT60 (T20 / T30)", "${t(res.t20Sec)} / ${t(res.t30Sec)}")
                    IrRow("EDT", t(res.edtSec))
                    IrRow(
                        "Clarity C50 / C80",
                        "%.1f / %.1f dB".format(res.c50Db, res.c80Db),
                    )
                    IrRow("Clock drift", "%+.1f ppm".format(res.driftPpm))
                    IrRow(
                        "Sync quality (pre / post)",
                        "%.2f / %.2f".format(res.preambleQuality, res.postambleQuality),
                    )
                    if (res.avgCount > 1) IrRow("Coherent averages", "${res.avgCount}")
                }
            }

            val context = androidx.compose.ui.platform.LocalContext.current
            var pendingCsv by remember { mutableStateOf<String?>(null) }
            val saveLauncher = androidx.activity.compose.rememberLauncherForActivityResult(
                androidx.activity.result.contract.ActivityResultContracts.CreateDocument("text/csv"),
            ) { uri ->
                val content = pendingCsv
                if (uri != null && content != null) viewModel.writeCsvTo(uri, content)
                pendingCsv = null
            }
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                OutlinedButton(onClick = {
                    viewModel.buildIrCsv()?.let { csv ->
                        val uri = viewModel.shareableCsv("ir_response", csv)
                        val send = android.content.Intent(android.content.Intent.ACTION_SEND).apply {
                            type = "text/csv"
                            putExtra(android.content.Intent.EXTRA_STREAM, uri)
                            addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
                        }
                        context.startActivity(
                            android.content.Intent.createChooser(send, "Share IR response"),
                        )
                    }
                }) { Text("Share CSV") }
                OutlinedButton(onClick = {
                    viewModel.buildIrCsv()?.let { csv ->
                        pendingCsv = csv
                        saveLauncher.launch("ir_response.csv")
                    }
                }) { Text("Save CSV") }
                OutlinedButton(onClick = {
                    viewModel.buildIrWav()?.let { bytes ->
                        pendingWav = bytes
                        wavLauncher.launch("impulse_response.wav")
                    }
                }) { Text("Save IR WAV") }
            }

            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                listOf("ETC", "Magnitude", "Group delay").forEachIndexed { i, label ->
                    FilterChip(
                        selected = view == i,
                        onClick = { view = i },
                        label = { Text(label) },
                    )
                }
            }

            val calCorr = remember(state.cal, state.irVersion) { viewModel.irCalCorrection() }
            when (view) {
                0 -> EtcPlot(
                    viewModel, res.irSamples / res.fs,
                    Modifier.fillMaxWidth().height(280.dp),
                )
                else -> IrFreqPlot(
                    viewModel, res.magBinHz, groupDelay = view == 2,
                    calCorr = calCorr,
                    modifier = Modifier.fillMaxWidth().height(280.dp),
                )
            }
        }
    }
}

@Composable
private fun IrRow(label: String, value: String) {
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, style = MaterialTheme.typography.bodyMedium)
        Text(value, style = MaterialTheme.typography.bodyMedium, fontFamily = FontFamily.Monospace)
    }
}

@Composable
private fun EtcPlot(viewModel: MainViewModel, spanSec: Double, modifier: Modifier) {
    val textMeasurer = rememberTextMeasurer()
    val gridColor = MaterialTheme.colorScheme.outlineVariant
    val labelColor = MaterialTheme.colorScheme.onSurfaceVariant
    val traceColor = MaterialTheme.colorScheme.primary
    val labelStyle = TextStyle(fontSize = 10.sp, color = labelColor)

    Canvas(modifier = modifier) {
        val leftPad = 40.dp.toPx()
        val bottomPad = 20.dp.toPx()
        val plotW = size.width - leftPad
        val plotH = size.height - bottomPad
        if (!viewModel.irEtcValid || plotW <= 0) return@Canvas

        val etc = viewModel.irEtc
        val yTop = 0f
        val yBot = -80f
        fun yFor(db: Float) =
            (plotH - (db.coerceIn(yBot, yTop) - yBot) / (yTop - yBot) * plotH)

        var db = 0f
        while (db >= yBot) {
            val y = yFor(db)
            drawLine(gridColor, Offset(leftPad, y), Offset(size.width, y), 1f)
            drawText(textMeasurer, "%.0f".format(db), style = labelStyle,
                topLeft = Offset(0f, y - 7.dp.toPx()))
            db -= 20f
        }
        val msSpan = spanSec * 1000.0
        val tStep = listOf(10.0, 25.0, 50.0, 100.0, 250.0, 500.0).firstOrNull { msSpan / it <= 8 } ?: 1000.0
        var t = 0.0
        while (t <= msSpan) {
            val x = leftPad + (t / msSpan * plotW).toFloat()
            drawLine(gridColor, Offset(x, 0f), Offset(x, plotH), 1f)
            drawText(textMeasurer, "%.0f".format(t), style = labelStyle,
                topLeft = Offset(x + 2.dp.toPx(), plotH + 2.dp.toPx()))
            t += tStep
        }

        val path = Path()
        for (i in etc.indices) {
            val x = leftPad + i.toFloat() / etc.size * plotW
            val y = yFor(etc[i])
            if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
        }
        drawPath(path, traceColor, style = Stroke(1.5.dp.toPx()))
        drawText(textMeasurer, "ETC dB vs ms", style = labelStyle,
            topLeft = Offset(leftPad + 4.dp.toPx(), 2.dp.toPx()))
    }
}

@Composable
private fun IrFreqPlot(
    viewModel: MainViewModel,
    binHz: Double,
    groupDelay: Boolean,
    calCorr: FloatArray?,
    modifier: Modifier,
) {
    val textMeasurer = rememberTextMeasurer()
    val gridColor = MaterialTheme.colorScheme.outlineVariant
    val labelColor = MaterialTheme.colorScheme.onSurfaceVariant
    val traceColor = MaterialTheme.colorScheme.primary
    val labelStyle = TextStyle(fontSize = 10.sp, color = labelColor)

    Canvas(modifier = modifier) {
        val leftPad = 44.dp.toPx()
        val bottomPad = 20.dp.toPx()
        val plotW = size.width - leftPad
        val plotH = size.height - bottomPad
        val bins = viewModel.irMagBins
        if (bins == 0 || binHz <= 0 || plotW <= 0) return@Canvas
        // Mic-cal shape applies to magnitude only (not group delay).
        val data = when {
            groupDelay -> viewModel.irGdMs
            calCorr != null -> FloatArray(bins) { viewModel.irMagDb[it] + calCorr[it] }
            else -> viewModel.irMagDb
        }

        val logMin = log10(20.0)
        val logMax = log10(20000.0)
        fun xFor(f: Double) =
            leftPad + ((log10(f) - logMin) / (logMax - logMin) * plotW).toFloat()

        // Range from in-band data.
        var lo = Float.MAX_VALUE
        var hi = -Float.MAX_VALUE
        for (i in 1 until bins) {
            val f = i * binHz
            if (f < 20.0 || f > 20000.0) continue
            val v = data[i]
            if (v < lo) lo = v
            if (v > hi) hi = v
        }
        if (hi <= lo) return@Canvas
        val step = if (groupDelay) 5f else 10f
        val yTop = ceil(hi / step) * step + step
        val yBot = kotlin.math.floor(lo / step) * step - step
        fun yFor(v: Float) =
            (plotH - (v.coerceIn(yBot, yTop) - yBot) / (yTop - yBot) * plotH)

        for (decade in intArrayOf(10, 100, 1000, 10000)) {
            for (mult in intArrayOf(1, 2, 5)) {
                val f = decade.toDouble() * mult
                if (f < 20.0 || f > 20000.0) continue
                val x = xFor(f)
                drawLine(gridColor, Offset(x, 0f), Offset(x, plotH), 1f)
                val label = if (f >= 1000) "${(f / 1000).roundToInt()}k" else "${f.roundToInt()}"
                drawText(textMeasurer, label, style = labelStyle,
                    topLeft = Offset(x + 2.dp.toPx(), plotH + 2.dp.toPx()))
            }
        }
        var g = yBot
        while (g <= yTop) {
            val y = yFor(g)
            drawLine(gridColor, Offset(leftPad, y), Offset(size.width, y), 1f)
            drawText(textMeasurer, "%.0f".format(g), style = labelStyle,
                topLeft = Offset(0f, y - 7.dp.toPx()))
            g += step * 2
        }

        val path = Path()
        var pen = false
        var prevBin = 0
        val cols = plotW.toInt()
        for (c in 0 until cols) {
            val fLo = 10.0.pow(logMin + (logMax - logMin) * c / cols)
            val fHi = 10.0.pow(logMin + (logMax - logMin) * (c + 1.0) / cols)
            var b0 = max(1, (fLo / binHz).toInt())
            var b1 = (fHi / binHz).toInt() + 1
            if (b0 <= prevBin) b0 = prevBin + 1
            if (b1 > bins) b1 = bins
            if (b0 >= b1) continue
            var v = 0f
            var cnt = 0
            for (i in b0 until b1) {
                v += data[i]
                cnt++
            }
            v /= cnt
            prevBin = b1 - 1
            val x = leftPad + c.toFloat()
            val y = yFor(v)
            if (pen) path.lineTo(x, y) else { path.moveTo(x, y); pen = true }
        }
        drawPath(path, traceColor, style = Stroke(2.dp.toPx()))
        drawText(
            textMeasurer,
            when {
                groupDelay -> "Excess group delay (ms)"
                calCorr != null -> "Magnitude (rel dB, mic cal applied)"
                else -> "Magnitude (rel dB, no mic cal)"
            },
            style = labelStyle,
            topLeft = Offset(leftPad + 4.dp.toPx(), 2.dp.toPx()),
        )
    }
}
