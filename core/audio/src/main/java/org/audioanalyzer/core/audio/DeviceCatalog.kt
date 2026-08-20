package org.audioanalyzer.core.audio

import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioManager

/** UI-friendly description of an audio input device. */
data class InputDevice(
    val id: Int,
    val label: String,
    val typeName: String,
    val isUsb: Boolean,
    val sampleRates: List<Int>,
    val channelCounts: List<Int>,
)

object DeviceCatalog {

    /** All available capture devices, USB devices first (measurement mics). */
    fun inputDevices(context: Context): List<InputDevice> {
        val am = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        return am.getDevices(AudioManager.GET_DEVICES_INPUTS)
            .map { it.toInputDevice() }
            .sortedByDescending { it.isUsb }
    }

    /** Whether the platform claims support for the UNPROCESSED input source. */
    fun supportsUnprocessed(context: Context): Boolean {
        val am = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        return am.getProperty(AudioManager.PROPERTY_SUPPORT_AUDIO_SOURCE_UNPROCESSED) == "true"
    }

    private fun AudioDeviceInfo.toInputDevice(): InputDevice {
        val type = typeName(type)
        val name = productName?.toString()?.takeIf { it.isNotBlank() } ?: type
        return InputDevice(
            id = id,
            label = "$name ($type)",
            typeName = type,
            isUsb = this.type == AudioDeviceInfo.TYPE_USB_DEVICE ||
                this.type == AudioDeviceInfo.TYPE_USB_HEADSET,
            sampleRates = sampleRates.toList().sorted(),
            channelCounts = channelCounts.toList().sorted(),
        )
    }

    private fun typeName(type: Int): String = when (type) {
        AudioDeviceInfo.TYPE_BUILTIN_MIC -> "Built-in mic"
        AudioDeviceInfo.TYPE_USB_DEVICE -> "USB device"
        AudioDeviceInfo.TYPE_USB_HEADSET -> "USB headset"
        AudioDeviceInfo.TYPE_WIRED_HEADSET -> "Wired headset"
        AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> "Bluetooth SCO"
        AudioDeviceInfo.TYPE_FM_TUNER -> "FM tuner"
        AudioDeviceInfo.TYPE_TELEPHONY -> "Telephony"
        AudioDeviceInfo.TYPE_REMOTE_SUBMIX -> "Remote submix"
        else -> "Type $type"
    }
}
