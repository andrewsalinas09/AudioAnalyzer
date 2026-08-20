// JNI bridge for org.audioanalyzer.core.audio.NativeEngine. Keep this file
// mechanical: no logic beyond marshalling.
#include <jni.h>

#include "AudioEngine.h"

extern "C" {

JNIEXPORT jint JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeStart(
    JNIEnv* /*env*/, jobject /*thiz*/, jint deviceId, jint sampleRate,
    jint channelCount, jint inputPreset) {
    return aa::AudioEngine::instance().start(deviceId, sampleRate, channelCount,
                                             inputPreset);
}

JNIEXPORT void JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeStop(JNIEnv* /*env*/,
                                                          jobject /*thiz*/) {
    aa::AudioEngine::instance().stop();
}

JNIEXPORT void JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeSnapshot(
    JNIEnv* env, jobject /*thiz*/, jdoubleArray out) {
    const jsize len = env->GetArrayLength(out);
    if (len < aa::kSnapshotSize) return;
    double buf[aa::kSnapshotSize];
    aa::AudioEngine::instance().snapshot(buf, aa::kSnapshotSize);
    env->SetDoubleArrayRegion(out, 0, aa::kSnapshotSize, buf);
}

JNIEXPORT jint JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeSnapshotSize(
    JNIEnv* /*env*/, jobject /*thiz*/) {
    return aa::kSnapshotSize;
}

JNIEXPORT void JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeSplConfigure(
    JNIEnv* /*env*/, jobject /*thiz*/, jint weighting, jint timeWeighting) {
    aa::AudioEngine::instance().splConfigure(weighting, timeWeighting);
}

JNIEXPORT void JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeSplResetStats(
    JNIEnv* /*env*/, jobject /*thiz*/) {
    aa::AudioEngine::instance().splResetStats();
}

JNIEXPORT void JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeSpectrumConfigure(
    JNIEnv* /*env*/, jobject /*thiz*/, jint fftSize, jint window,
    jdouble avgTauSec) {
    aa::AudioEngine::instance().spectrumConfigure(fftSize, window, avgTauSec);
}

JNIEXPORT jint JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeSpectrumRead(
    JNIEnv* env, jobject /*thiz*/, jfloatArray avgOut, jfloatArray peakOut,
    jboolean psd) {
    const jsize avgLen = env->GetArrayLength(avgOut);
    const jsize peakLen = env->GetArrayLength(peakOut);
    const jsize maxBins = avgLen < peakLen ? avgLen : peakLen;
    jfloat* avg = env->GetFloatArrayElements(avgOut, nullptr);
    jfloat* peak = env->GetFloatArrayElements(peakOut, nullptr);
    const jint bins = aa::AudioEngine::instance().spectrumRead(
        avg, peak, maxBins, psd == JNI_TRUE);
    env->ReleaseFloatArrayElements(avgOut, avg, bins > 0 ? 0 : JNI_ABORT);
    env->ReleaseFloatArrayElements(peakOut, peak, bins > 0 ? 0 : JNI_ABORT);
    return bins;
}

JNIEXPORT void JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeSpectrumResetPeak(
    JNIEnv* /*env*/, jobject /*thiz*/) {
    aa::AudioEngine::instance().spectrumResetPeak();
}

}  // extern "C"
