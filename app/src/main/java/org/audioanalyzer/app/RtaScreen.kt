package org.audioanalyzer.app

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AssistChip
import androidx.compose.material3.Button
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
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.roundToInt

private val fftSizes = listOf(4096, 8192, 16384, 32768)
private val avgChoices = listOf(0.0, 0.5, 2.0, 8.0)
private val smoothChoices = listOf(0, 48, 24, 12, 6, 3)
private const val kFMin = 20.0
private const val kFMax = 20000.0

@Composable
fun RtaScreen(viewModel: MainViewModel) {
    val state by viewModel.state.collectAsState()
    val calCorr = remember(state.cal, viewModel.rtaBins, viewModel.rtaBinHz) {
        RtaMath.calCorrection(
            state.cal.calibration, state.cal.totalOffsetDb,
            viewModel.rtaBins, viewModel.rtaBinHz,
        )
    }
    // Recomputed whenever a new spectrum frame arrives.
    val traces = remember(state.rtaVersion, state.rtaSmoothing) {
        val n = viewModel.rtaBins
        if (n == 0) null else Triple(
            RtaMath.smooth(viewModel.rtaAvg, n, state.rtaSmoothing),
            RtaMath.smooth(viewModel.rtaPeak, n, state.rtaSmoothing),
            viewModel.rtaBinHz,
        )
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Text("RTA", style = MaterialTheme.typography.headlineSmall)

        SpectrumPlot(
            traces = traces,
            calCorr = calCorr,
            calibrated = state.cal.totalOffsetDb != null,
            psd = state.rtaPsd,
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f),
        )

        // Cycling config chips: tap to advance to the next value.
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            AssistChip(
                onClick = {
                    val next = fftSizes[(fftSizes.indexOf(state.rtaFftSize) + 1) % fftSizes.size]
                    viewModel.setRtaConfig(fftSize = next)
                },
                label = { Text("FFT ${state.rtaFftSize / 1024}k") },
            )
            AssistChip(
                onClick = {
                    val entries = org.audioanalyzer.core.audio.SpectrumWindow.entries
                    val next = entries[(state.rtaWindow.ordinal + 1) % entries.size]
                    viewModel.setRtaConfig(window = next)
                },
                label = { Text(state.rtaWindow.label) },
            )
            AssistChip(
                onClick = {
                    val i = avgChoices.indexOf(state.rtaAvgTauSec)
                    viewModel.setRtaConfig(avgTauSec = avgChoices[(i + 1) % avgChoices.size])
                },
                label = {
                    Text(if (state.rtaAvgTauSec == 0.0) "Avg off" else "Avg ${state.rtaAvgTauSec}s")
                },
            )
            AssistChip(
                onClick = {
                    val i = smoothChoices.indexOf(state.rtaSmoothing)
                    viewModel.setRtaSmoothing(smoothChoices[(i + 1) % smoothChoices.size])
                },
                label = {
                    Text(if (state.rtaSmoothing == 0) "Smooth off" else "1/${state.rtaSmoothing}")
                },
            )
            AssistChip(
                onClick = { viewModel.setRtaPsd(!state.rtaPsd) },
                label = { Text(if (state.rtaPsd) "PSD" else "Spectrum") },
            )
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
            if (state.running) {
                Button(onClick = viewModel::stop) { Text("Stop") }
            } else {
                Button(onClick = viewModel::start) { Text("Start") }
            }
            OutlinedButton(onClick = viewModel::resetRtaPeak) { Text("Clear peak") }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            OutlinedButton(
                onClick = {
                    viewModel.buildRtaCsv()?.let { csv ->
                        val uri = viewModel.shareableCsv("rta", csv)
                        val send = android.content.Intent(android.content.Intent.ACTION_SEND).apply {
                            type = "text/csv"
                            putExtra(android.content.Intent.EXTRA_STREAM, uri)
                            addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
                        }
                        context.startActivity(
                            android.content.Intent.createChooser(send, "Share RTA spectrum"),
                        )
                    }
                },
                enabled = traces != null,
            ) { Text("Share CSV") }
            OutlinedButton(
                onClick = {
                    viewModel.buildRtaCsv()?.let { csv ->
                        pendingCsv = csv
                        saveLauncher.launch("rta_spectrum.csv")
                    }
                },
                enabled = traces != null,
            ) { Text("Save CSV") }
        }
    }
}

@Composable
private fun SpectrumPlot(
    traces: Triple<FloatArray, FloatArray, Double>?,
    calCorr: FloatArray,
    calibrated: Boolean,
    psd: Boolean,
    modifier: Modifier = Modifier,
) {
    val textMeasurer = rememberTextMeasurer()
    val gridColor = MaterialTheme.colorScheme.outlineVariant
    val labelColor = MaterialTheme.colorScheme.onSurfaceVariant
    val avgColor = MaterialTheme.colorScheme.primary
    val peakColor = MaterialTheme.colorScheme.tertiary
    val cursorColor = MaterialTheme.colorScheme.secondary
    val labelStyle = TextStyle(fontSize = 10.sp, color = labelColor)
    var cursorFrac by rememberSaveable { mutableStateOf<Float?>(null) }
    // Sticky display range with hysteresis so the plot doesn't pump.
    var yTop by remember { mutableStateOf(if (calibrated) 100f else -20f) }
    var yBot by remember { mutableStateOf(if (calibrated) 0f else -120f) }

    Canvas(
        modifier = modifier.pointerInput(Unit) {
            detectTapGestures { pos ->
                cursorFrac = if (cursorFrac != null &&
                    kotlin.math.abs((cursorFrac ?: 0f) - pos.x / size.width) < 0.02f
                ) null else pos.x / size.width
            }
        },
    ) {
        val leftPad = 44.dp.toPx()
        val bottomPad = 20.dp.toPx()
        val plotW = size.width - leftPad
        val plotH = size.height - bottomPad
        if (plotW <= 0 || plotH <= 0) return@Canvas

        val logMin = log10(kFMin)
        val logMax = log10(kFMax)
        fun xFor(f: Double) =
            leftPad + ((log10(f) - logMin) / (logMax - logMin) * plotW).toFloat()

        // Frequency grid: decades and 2/5 subdivisions.
        for (decade in intArrayOf(10, 100, 1000, 10000)) {
            for (mult in intArrayOf(1, 2, 5)) {
                val f = decade.toDouble() * mult
                if (f < kFMin || f > kFMax) continue
                val x = xFor(f)
                drawLine(gridColor, Offset(x, 0f), Offset(x, plotH), 1f)
                val label = if (f >= 1000) "${(f / 1000).roundToInt()}k" else "${f.roundToInt()}"
                drawText(
                    textMeasurer, label, style = labelStyle,
                    topLeft = Offset(x + 2.dp.toPx(), plotH + 2.dp.toPx()),
                )
            }
        }
        val x20k = xFor(20000.0)
        drawLine(gridColor, Offset(x20k, 0f), Offset(x20k, plotH), 1f)

        if (traces != null) {
            val (avg, peak, binHz) = traces
            val bins = avg.size

            fun corrected(a: FloatArray, i: Int): Float =
                a[i] + if (i < calCorr.size) calCorr[i] else 0f

            // Autorange with hysteresis from the average trace.
            var lo = Float.MAX_VALUE
            var hi = -Float.MAX_VALUE
            for (i in 1 until bins) {
                val f = i * binHz
                if (f < kFMin || f > kFMax) continue
                val v = corrected(avg, i)
                if (v < lo) lo = v
                if (v > hi) hi = v
            }
            if (hi > lo) {
                val wantTop = (ceil(hi / 10.0) * 10 + 10).toFloat()
                val wantBot = (floor(lo / 10.0) * 10 - 10).toFloat()
                if (wantTop > yTop || wantTop < yTop - 15f) yTop = wantTop
                if (wantBot < yBot || wantBot > yBot + 15f) yBot = wantBot
            }

            fun yFor(db: Float) =
                (plotH - (db - yBot) / (yTop - yBot) * plotH).coerceIn(0f, plotH)

            // dB grid.
            val dbStep = listOf(5f, 10f, 20f, 40f).first { (yTop - yBot) / it <= 10 }
            var db = ceil(yBot / dbStep) * dbStep
            while (db <= yTop) {
                val y = yFor(db)
                drawLine(gridColor, Offset(leftPad, y), Offset(size.width, y), 1f)
                drawText(
                    textMeasurer, "%.0f".format(db), style = labelStyle,
                    topLeft = Offset(0f, y - 7.dp.toPx()),
                )
                db += dbStep
            }

            // Per-pixel-column max over the bins mapped to that column.
            fun tracePath(a: FloatArray): Path {
                val path = Path()
                val cols = plotW.toInt()
                var pen = false
                var prevBin = 0
                for (c in 0 until cols) {
                    val fLo = 10.0.pow(logMin + (logMax - logMin) * c / cols)
                    val fHi = 10.0.pow(logMin + (logMax - logMin) * (c + 1.0) / cols)
                    var b0 = max(1, (fLo / binHz).toInt())
                    var b1 = (fHi / binHz).toInt() + 1
                    if (b0 <= prevBin) b0 = prevBin + 1
                    if (b1 > bins) b1 = bins
                    if (b0 >= b1) continue
                    var v = -Float.MAX_VALUE
                    for (i in b0 until b1) {
                        val cv = corrected(a, i)
                        if (cv > v) v = cv
                    }
                    prevBin = b1 - 1
                    val x = leftPad + c.toFloat()
                    val y = yFor(v)
                    if (pen) path.lineTo(x, y) else { path.moveTo(x, y); pen = true }
                }
                return path
            }

            drawPath(tracePath(peak), peakColor.copy(alpha = 0.45f), style = Stroke(1.5.dp.toPx()))
            drawPath(tracePath(avg), avgColor, style = Stroke(2.dp.toPx()))

            // Tap cursor: frequency + level readout.
            cursorFrac?.let { frac ->
                val x = (frac * size.width).coerceIn(leftPad, size.width)
                val f = 10.0.pow(logMin + (logMax - logMin) * ((x - leftPad) / plotW))
                val bin = (f / binHz).roundToInt().coerceIn(1, bins - 1)
                val v = corrected(avg, bin)
                drawLine(cursorColor, Offset(x, 0f), Offset(x, plotH), 1.5.dp.toPx())
                val fLabel = if (f >= 1000) "%.2f kHz".format(f / 1000) else "%.0f Hz".format(f)
                val unit = if (psd) "dB/Hz" else "dB"
                drawText(
                    textMeasurer, "$fLabel   %.1f $unit".format(v),
                    style = TextStyle(fontSize = 12.sp, color = cursorColor),
                    topLeft = Offset(leftPad + 4.dp.toPx(), 2.dp.toPx()),
                )
            }
        } else {
            drawText(
                textMeasurer, "Start the stream to see the spectrum",
                style = labelStyle,
                topLeft = Offset(leftPad + 8.dp.toPx(), plotH / 2),
            )
        }

        drawLine(gridColor, Offset(leftPad, 0f), Offset(leftPad, plotH), 1f)
        drawLine(gridColor, Offset(leftPad, plotH), Offset(size.width, plotH), 1f)
    }
}
