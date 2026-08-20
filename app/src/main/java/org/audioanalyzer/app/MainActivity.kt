package org.audioanalyzer.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.google.accompanist.permissions.ExperimentalPermissionsApi
import com.google.accompanist.permissions.isGranted
import com.google.accompanist.permissions.rememberPermissionState

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                Surface { AppRoot() }
            }
        }
    }
}

private val tabs = listOf("SPL", "RTA", "Log", "Health", "Cal")

@OptIn(ExperimentalPermissionsApi::class)
@Composable
private fun AppRoot() {
    val micPermission = rememberPermissionState(android.Manifest.permission.RECORD_AUDIO)
    if (!micPermission.status.isGranted) {
        Column(
            modifier = Modifier.fillMaxSize().padding(24.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text("AudioAnalyzer needs microphone access to measure anything.")
            Button(
                onClick = { micPermission.launchPermissionRequest() },
                modifier = Modifier.padding(top = 16.dp),
            ) { Text("Grant microphone access") }
        }
        return
    }

    val viewModel = viewModel<MainViewModel>()
    var tab by rememberSaveable { mutableIntStateOf(0) }
    androidx.compose.runtime.LaunchedEffect(tab) { viewModel.setRtaActive(tab == 1) }

    Scaffold(
        bottomBar = {
            NavigationBar {
                tabs.forEachIndexed { i, label ->
                    NavigationBarItem(
                        selected = tab == i,
                        onClick = { tab = i },
                        icon = {},
                        label = { Text(label) },
                    )
                }
            }
        },
    ) { padding ->
        androidx.compose.foundation.layout.Box(modifier = Modifier.padding(padding)) {
            when (tab) {
                0 -> SplScreen(viewModel)
                1 -> RtaScreen(viewModel)
                2 -> LogScreen(viewModel)
                3 -> AudioHealthScreen(viewModel)
                else -> CalibrationScreen(viewModel)
            }
        }
    }
}
