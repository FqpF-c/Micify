#include <jni.h>
#include <string>
#include "streamer.h"

namespace {
micify::Streamer g_streamer;

std::string jstringToStd(JNIEnv *env, jstring s) {
    const char *chars = env->GetStringUTFChars(s, nullptr);
    std::string result(chars);
    env->ReleaseStringUTFChars(s, chars);
    return result;
}
} // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_micify_app_NativeStreamer_start(JNIEnv *env, jobject /*thiz*/, jstring host, jint port,
                                          jboolean useTcp) {
    return g_streamer.start(jstringToStd(env, host), (int) port, useTcp == JNI_TRUE)
               ? JNI_TRUE
               : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_micify_app_NativeStreamer_stop(JNIEnv * /*env*/, jobject /*thiz*/) {
    g_streamer.stop();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_micify_app_NativeStreamer_isStreaming(JNIEnv * /*env*/, jobject /*thiz*/) {
    return g_streamer.isStreaming() ? JNI_TRUE : JNI_FALSE;
}
