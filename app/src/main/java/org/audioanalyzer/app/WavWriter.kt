package org.audioanalyzer.app

import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Minimal RIFF/WAVE writer.
 *  - 16-bit PCM (format 1): maximum player compatibility — used for the
 *    exported sweep so AVRs/phones/PCs all play it.
 *  - 32-bit float (format 3): bit-faithful — used for measured IRs so
 *    analysis tools (e.g. REW import) see exactly what we computed.
 */
object WavWriter {

    fun pcm16(samples: FloatArray, sampleRate: Int): ByteArray =
        build(samples, sampleRate, bitsPerSample = 16, floatFormat = false)

    fun float32(samples: FloatArray, sampleRate: Int): ByteArray =
        build(samples, sampleRate, bitsPerSample = 32, floatFormat = true)

    private fun build(
        samples: FloatArray,
        sampleRate: Int,
        bitsPerSample: Int,
        floatFormat: Boolean,
    ): ByteArray {
        val bytesPerSample = bitsPerSample / 8
        val dataSize = samples.size * bytesPerSample
        val out = ByteArrayOutputStream(44 + dataSize)
        val header = ByteBuffer.allocate(44).order(ByteOrder.LITTLE_ENDIAN)
        header.put("RIFF".toByteArray())
        header.putInt(36 + dataSize)
        header.put("WAVE".toByteArray())
        header.put("fmt ".toByteArray())
        header.putInt(16)
        header.putShort(if (floatFormat) 3 else 1) // IEEE float / PCM
        header.putShort(1) // mono
        header.putInt(sampleRate)
        header.putInt(sampleRate * bytesPerSample)
        header.putShort(bytesPerSample.toShort())
        header.putShort(bitsPerSample.toShort())
        header.put("data".toByteArray())
        header.putInt(dataSize)
        out.write(header.array())

        val data = ByteBuffer.allocate(dataSize).order(ByteOrder.LITTLE_ENDIAN)
        if (floatFormat) {
            for (s in samples) data.putFloat(s)
        } else {
            for (s in samples) {
                val v = (s.coerceIn(-1f, 1f) * 32767.0f).toInt()
                data.putShort(v.toShort())
            }
        }
        out.write(data.array())
        return out.toByteArray()
    }
}
