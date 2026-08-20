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

JNIEXPORT jint JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeGenStartTone(
    JNIEnv* /*env*/, jobject /*thiz*/, jint deviceId, jint kind,
    jdouble freqHz, jdouble levelDb) {
    return aa::AudioEngine::instance().genStartTone(deviceId, kind, freqHz,
                                                    levelDb);
}

JNIEXPORT jint JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeGenStartSweep(
    JNIEnv* /*env*/, jobject /*thiz*/, jint deviceId, jboolean exponential,
    jdouble f1, jdouble f2, jdouble durationSec, jdouble levelDb,
    jboolean syncFrame) {
    return aa::AudioEngine::instance().genStartSweep(
        deviceId, exponential == JNI_TRUE, f1, f2, durationSec, levelDb,
        syncFrame == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeGenSetTone(
    JNIEnv* /*env*/, jobject /*thiz*/, jdouble freqHz, jdouble levelDb) {
    aa::AudioEngine::instance().genSetTone(freqHz, levelDb);
}

JNIEXPORT void JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeGenStop(
    JNIEnv* /*env*/, jobject /*thiz*/) {
    aa::AudioEngine::instance().genStop();
}

JNIEXPORT jint JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeIrBeginCapture(
    JNIEnv* /*env*/, jobject /*thiz*/, jdouble seconds) {
    return aa::AudioEngine::instance().irBeginCapture(seconds);
}

JNIEXPORT void JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeIrAbort(JNIEnv* /*env*/,
                                                             jobject /*thiz*/) {
    aa::AudioEngine::instance().irAbort();
}

JNIEXPORT jint JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeIrState(
    JNIEnv* /*env*/, jobject /*thiz*/) {
    return aa::AudioEngine::instance().irState();
}

JNIEXPORT jdouble JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeIrCapturedSec(
    JNIEnv* /*env*/, jobject /*thiz*/) {
    return aa::AudioEngine::instance().irCapturedSec();
}

JNIEXPORT jint JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeIrAnalyze(
    JNIEnv* /*env*/, jobject /*thiz*/, jdouble f1, jdouble f2,
    jdouble durationSec) {
    return aa::AudioEngine::instance().irAnalyze(f1, f2, durationSec);
}

JNIEXPORT void JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeIrSummary(
    JNIEnv* env, jobject /*thiz*/, jdoubleArray out) {
    const jsize len = env->GetArrayLength(out);
    if (len < aa::AudioEngine::kIrSummarySize) return;
    double buf[aa::AudioEngine::kIrSummarySize];
    aa::AudioEngine::instance().irSummary(buf, aa::AudioEngine::kIrSummarySize);
    env->SetDoubleArrayRegion(out, 0, aa::AudioEngine::kIrSummarySize, buf);
}

JNIEXPORT jint JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeIrEtc(
    JNIEnv* env, jobject /*thiz*/, jfloatArray out) {
    const jsize len = env->GetArrayLength(out);
    jfloat* buf = env->GetFloatArrayElements(out, nullptr);
    const jint n = aa::AudioEngine::instance().irEtc(buf, len);
    env->ReleaseFloatArrayElements(out, buf, n > 0 ? 0 : JNI_ABORT);
    return n;
}

JNIEXPORT jint JNICALL
Java_org_audioanalyzer_core_audio_NativeEngine_nativeIrMag(
    JNIEnv* env, jobject /*thiz*/, jfloatArray magOut, jfloatArray gdOut) {
    const jsize magLen = env->GetArrayLength(magOut);
    const jsize gdLen = env->GetArrayLength(gdOut);
    const jsize maxBins = magLen < gdLen ? magLen : gdLen;
    jfloat* mag = env->GetFloatArrayElements(magOut, nullptr);
    jfloat* gd = env->GetFloatArrayElements(gdOut, nullptr);
    const jint n = aa::AudioEngine::instance().irMag(mag, gd, maxBins);
    env->ReleaseFloatArrayElements(magOut, mag, n > 0 ? 0 : JNI_ABORT);
    env->ReleaseFloatArrayElements(gdOut, gd, n > 0 ? 0 : JNI_ABORT);
    return n;
}

}  // extern "C"
