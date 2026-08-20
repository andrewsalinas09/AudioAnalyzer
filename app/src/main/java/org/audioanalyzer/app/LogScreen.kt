package org.audioanalyzer.app

import android.content.Intent
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.max

private data class WindowChoice(val label: String, val seconds: Double?)

private val windowChoices = listOf(
    WindowChoice("30 s", 30.0),
    WindowChoice("5 min", 300.0),
    WindowChoice("30 min", 1800.0),
    WindowChoice("All", null),
)

@Composable
fun LogScreen(viewModel: MainViewModel) {
    val state by viewModel.state.collectAsState()
    val context = LocalContext.current
    var windowIdx by rememberSaveable { mutableIntStateOf(0) }

    // Re-read the visible slice whenever the log advances.
    val points = remember(state.logVersion, windowIdx) {
        viewModel.visiblePoints(windowChoices[windowIdx].seconds)
    }
    val offset = state.cal.totalOffsetDb

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("SPL over time", style = MaterialTheme.typography.headlineSmall)

        SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
            windowChoices.forEachIndexed { i, w ->
                SegmentedButton(
                    selected = windowIdx == i,
                    onClick = { windowIdx = i },
                    shape = SegmentedButtonDefaults.itemShape(i, windowChoices.size),
                ) { Text(w.label) }
            }
        }

        SplTimeChart(
            points = points,
            offsetDb = offset,
            modifier = Modifier
                .fillMaxWidth()
                .height(300.dp),
        )
        Text(
            (if (offset != null) "dB SPL" else "dBFS (uncalibrated)") +
                "  —  solid: ${points.lastOrNull()?.descriptor ?: "level"},  dashed: Leq",
            style = MaterialTheme.typography.bodySmall,
        )

        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            if (state.running) {
                Button(onClick = viewModel::stop) { Text("Stop") }
            } else {
                Button(onClick = viewModel::start) { Text("Start") }
            }
            OutlinedButton(
                onClick = {
                    val uri = viewModel.exportLogCsv()
                    val send = Intent(Intent.ACTION_SEND).apply {
                        type = "text/csv"
                        putExtra(Intent.EXTRA_STREAM, uri)
                        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    }
                    context.startActivity(Intent.createChooser(send, "Export SPL log"))
                },
                enabled = viewModel.logPoints.isNotEmpty(),
            ) { Text("Export CSV") }
            OutlinedButton(
                onClick = viewModel::clearLog,
                enabled = viewModel.logPoints.isNotEmpty(),
            ) { Text("Clear") }
        }

        var pendingCsv by androidx.compose.runtime.remember {
            androidx.compose.runtime.mutableStateOf<String?>(null)
        }
        val saveLauncher = androidx.activity.compose.rememberLauncherForActivityResult(
            androidx.activity.result.contract.ActivityResultContracts.CreateDocument("text/csv"),
        ) { uri ->
            val content = pendingCsv
            if (uri != null && content != null) viewModel.writeCsvTo(uri, content)
            pendingCsv = null
        }
        OutlinedButton(
            onClick = {
                pendingCsv = viewModel.buildLogCsv()
                saveLauncher.launch("spl_log.csv")
            },
            enabled = viewModel.logPoints.isNotEmpty(),
        ) { Text("Save CSV to Files") }

        val total = viewModel.logPoints.size
        if (total > 0) {
            val dur = viewModel.logPoints.last().tSec - viewModel.logPoints.first().tSec
            Text(
                "$total samples, %d:%02d logged".format(dur.toInt() / 60, dur.toInt() % 60),
                style = MaterialTheme.typography.bodySmall,
            )
        }
    }
}

@Composable
private fun SplTimeChart(
    points: List<SplLogPoint>,
    offsetDb: Double?,
    modifier: Modifier = Modifier,
) {
    val textMeasurer = rememberTextMeasurer()
    val gridColor = MaterialTheme.colorScheme.outlineVariant
    val labelColor = MaterialTheme.colorScheme.onSurfaceVariant
    val levelColor = MaterialTheme.colorScheme.primary
    val leqColor = MaterialTheme.colorScheme.tertiary
    val labelStyle = TextStyle(fontSize = 10.sp, color = labelColor)
    val off = offsetDb ?: 0.0

    Canvas(modifier = modifier) {
        val leftPad = 46.dp.toPx()
        val bottomPad = 22.dp.toPx()
        val plotW = size.width - leftPad
        val plotH = size.height - bottomPad
        if (plotW <= 0 || plotH <= 0) return@Canvas

        // Value range from the visible data, padded and snapped to 5 dB.
        val levels = points.flatMap {
            listOf(it.instantDbfs + off, it.leqDbfs + off)
        }.filter { it.isFinite() }
        var yMin = floor(((levels.minOrNull() ?: 20.0) - 4.0) / 5.0) * 5.0
        var yMax = ceil(((levels.maxOrNull() ?: 90.0) + 4.0) / 5.0) * 5.0
        if (yMax - yMin < 20.0) {
            val mid = (yMax + yMin) / 2
            yMin = mid - 10.0
            yMax = mid + 10.0
        }

        val tEnd = points.lastOrNull()?.tSec ?: 0.0
        val tStart = points.firstOrNull()?.tSec ?: 0.0
        val span = max(tEnd - tStart, 1.0)

        fun xFor(t: Double) = leftPad + ((t - tStart) / span * plotW).toFloat()
        fun yFor(db: Double) =
            (plotH - (db - yMin) / (yMax - yMin) * plotH).toFloat()

        // Horizontal dB grid.
        val dbStep = listOf(1.0, 2.0, 5.0, 10.0, 20.0).first { (yMax - yMin) / it <= 8 }
        var db = ceil(yMin / dbStep) * dbStep
        while (db <= yMax) {
            val y = yFor(db)
            drawLine(gridColor, Offset(leftPad, y), Offset(size.width, y), 1f)
            drawText(
                textMeasurer, "%.0f".format(db), style = labelStyle,
                topLeft = Offset(0f, y - 7.dp.toPx()),
            )
            db += dbStep
        }

        // Vertical time grid.
        val tSteps = listOf(5.0, 10.0, 30.0, 60.0, 300.0, 600.0, 1800.0, 3600.0)
        val tStep = tSteps.firstOrNull { span / it <= 7 } ?: 7200.0
        var t = ceil(tStart / tStep) * tStep
        while (t <= tEnd) {
            val x = xFor(t)
            drawLine(gridColor, Offset(x, 0f), Offset(x, plotH), 1f)
            val label = if (tStep >= 60.0) "%d:%02d".format(t.toInt() / 60, t.toInt() % 60)
            else "%.0fs".format(t)
            drawText(
                textMeasurer, label, style = labelStyle,
                topLeft = Offset(x + 2.dp.toPx(), plotH + 4.dp.toPx()),
            )
            t += tStep
        }

        if (points.size < 2) return@Canvas

        // Decimate to ~4 points per pixel and break the trace across gaps
        // (stream stopped) longer than a second.
        val stride = max(1, points.size / (plotW.toInt() * 4).coerceAtLeast(1))
        fun tracePath(value: (SplLogPoint) -> Double): Path {
            val path = Path()
            var pen = false
            var lastT = Double.NaN
            var i = 0
            while (i < points.size) {
                val p = points[i]
                val v = value(p) + off
                if (!v.isFinite()) { pen = false; i += stride; continue }
                if (pen && p.tSec - lastT > 1.0) pen = false
                val x = xFor(p.tSec)
                val y = yFor(v.coerceIn(yMin, yMax))
                if (pen) path.lineTo(x, y) else { path.moveTo(x, y); pen = true }
                lastT = p.tSec
                i += stride
            }
            return path
        }

        drawPath(tracePath { it.leqDbfs }, leqColor, style = Stroke(
            width = 2.dp.toPx(),
            pathEffect = PathEffect.dashPathEffect(floatArrayOf(8f, 8f)),
        ))
        drawPath(tracePath { it.instantDbfs }, levelColor, style = Stroke(2.dp.toPx()))

        // Frame.
        drawLine(gridColor, Offset(leftPad, 0f), Offset(leftPad, plotH), 1f)
        drawLine(gridColor, Offset(leftPad, plotH), Offset(size.width, plotH), 1f)
    }
}
