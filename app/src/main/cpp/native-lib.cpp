#include <jni.h>
#include <string>
#include <android/log.h>
#include <unistd.h>

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>

#define TAG "FFmpeg_JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,TAG,__VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,TAG,__VA_ARGS__)

#define ASSERT_EQ(x, y) do { if ((x) == (y)) ; else { LOGE( "0x%x != 0x%x\n", \
    (unsigned) (x), (unsigned) (y)); assert((x) == (y)); } } while (0)

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_example_androidffmpegtest_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    hello += avcodec_configuration();
    //LOGD("just for test");
    return env->NewStringUTF(hello.c_str());
}

static long long GetNowMs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int sec = tv.tv_sec % 360000;
    long long t = sec * 1000 + tv.tv_usec / 1000;
    return t;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_androidffmpegtest_MainActivity_testPerformance(JNIEnv *env, jobject instance,
                                                                jstring url_) {
    const char *url = env->GetStringUTFChars(url_, 0);
    AVFormatContext *pFormatCtx = NULL;
    AVCodec *codec = NULL;
    // TODO
    LOGD("url %s", url);
#if 0
    if (access("/mnt/oob", F_OK) == 0)
        LOGI("%s file existed", url);
    else
        LOGI("%s file not exist", url);
#endif
    int re = avformat_open_input(&pFormatCtx, url, 0, 0);
    if (re != 0) {
        LOGE("avformat_open_input failed!:%s", av_err2str(re));
    }

    re = avformat_find_stream_info(pFormatCtx, 0);
    if (re != 0) {
        LOGE("avformat_find_stream_info failed!");
    }

    int videoStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);

    AVCodecContext *vctx = avcodec_alloc_context3(codec);

    re = avcodec_parameters_to_context(vctx, pFormatCtx->streams[videoStream]->codecpar);
    if (re) {
        LOGE("avcodec_parameters_to_context error");
        return;
    }

    /* 设置解码线程 */
    vctx->thread_count = 2;

    re = avcodec_open2(vctx, codec, 0);
    if (re) {
        LOGE("avcodec_open2 error\n");
        return;
    }

    LOGD("vctx timebase = %d/ %d\n", vctx->time_base.num, vctx->time_base.den);
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    long long start = GetNowMs();
    int frameCount = 0;

    for (;;) {
        /* 每隔3S算一次fps平均值 */
        if (GetNowMs() - start >= 3000) {
            LOGI("now decode fps is %d, thread(%d)\n", frameCount / 3, vctx->thread_count);
            start = GetNowMs();
            frameCount = 0;
        }

        int re = av_read_frame(pFormatCtx, pkt);

        if (re != 0) {

            printf("读取到结尾处!\n");
            //int pos = 20 * r2d(ic->streams[videoStream]->time_base);
            //向前跳且是关键帧，避免花屏
            // usleep(1000 * 1000);
            av_seek_frame(pFormatCtx, videoStream, 0, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_FRAME);
            continue;
            //break;
        }
        if (pkt->stream_index == videoStream) {
            re = avcodec_send_packet(vctx, pkt);
            if (re < 0) {
                LOGE("Error sending a packet for decoding\n");
                return;
            }

            for (;;) {
                re = avcodec_receive_frame(vctx, frame);
                if (re == AVERROR(EAGAIN) || re == AVERROR_EOF) {
                    //printf("avcodec receive frame error\n");
                    /* 如果返回上面两个错误，说明这一帧解码完成 */
                    break;
                } else if (re < 0) {
                    LOGE("Error during decoding\n");
                    return;
                }

                frameCount++;
            }
        }
        /* 解码完成后引用计数减一，避免内存泄露 */
        av_packet_unref(pkt);
    }
    avformat_close_input(&pFormatCtx);
    av_packet_free(&pkt);
    av_frame_free(&frame);
    env->ReleaseStringUTFChars(url_, url);

}
static SLObjectItf engineObject = nullptr;

static SLEngineItf CreateSL() {
    SLresult result;
    SLEngineItf en;
    result = slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
    ASSERT_EQ(SL_RESULT_SUCCESS, result);
    // SL_BOOLEAN_FALSE阻塞式创建
    result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
    ASSERT_EQ(SL_RESULT_SUCCESS, result);
    SLEngineItf engineEngine;
    result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);
    ASSERT_EQ(SL_RESULT_SUCCESS, result);
    return engineEngine;

}

// Called after audio player empties a buffer of data
static void playerCallback(SLAndroidSimpleBufferQueueItf caller, void *context) {
    LOGD("##%s## path %s", __FUNCTION__, (char *) (context));
    //LOGD("##%s##",__FUNCTION__);
    static FILE *fp = NULL;
    static char *buf = NULL;
    if (!buf) {
        buf = new char[1024 * 1024];
    }
    if (!fp) {
        fp = fopen("/storage/emulated/0/1.pcm", "rb");
    }
    if (!fp)return;
    if (feof(fp) == 0) {
        int len = fread(buf, 1, 1024, fp);
        if (len > 0)
            (*caller)->Enqueue(caller, buf, len);
    }


}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_androidffmpegtest_MainActivity_testopenssl(JNIEnv *env, jobject instance,
                                                            jstring path_) {
    const char *path = env->GetStringUTFChars(path_, 0);
    char *path1 = const_cast<char *>(path);
    LOGD("path1 = %s", path1);
    SLresult result;
    //1.创建引擎
    SLEngineItf engineEngine = CreateSL();
    if (engineEngine)
        LOGD("createSL success");
    //env->ReleaseStringUTFChars(path_, path);

    // 2.创建输出混音器
    SLObjectItf outputmixObject;
    result = (*engineEngine)->CreateOutputMix(engineEngine, &outputmixObject, 0, NULL, NULL);
    ASSERT_EQ(SL_RESULT_SUCCESS, result);
    result = (*outputmixObject)->Realize(outputmixObject, SL_BOOLEAN_FALSE);
    ASSERT_EQ(SL_RESULT_SUCCESS, result);

    SLDataLocator_OutputMix outmix = {SL_DATALOCATOR_OUTPUTMIX, outputmixObject};
    SLDataSink audioSink = {&outmix, 0};

    // 3.配置音频信息
    // 缓冲队列
    SLDataLocator_AndroidSimpleBufferQueue que = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 10};
    // 音频格式配置
    SLDataFormat_PCM pcm = {
            .formatType=SL_DATAFORMAT_PCM,
            .numChannels =2,
            .samplesPerSec = SL_SAMPLINGRATE_44_1,
            .bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16,
            .containerSize = SL_PCMSAMPLEFORMAT_FIXED_16,
            .channelMask=SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
            .endianness = SL_BYTEORDER_LITTLEENDIAN
    };

    SLDataSource ds = {&que, &pcm};

    // 4.创建播放器
    SLObjectItf playerObject = nullptr;
    SLPlayItf playerPlay = nullptr;
    SLAndroidSimpleBufferQueueItf playerBufferQueue = nullptr;
    // 表示需要这个接口，待会下面的GetInterface才能获取到
    const SLInterfaceID ids[] = {SL_IID_BUFFERQUEUE};
    // 表示接口开放还是关闭，TRUE表示开放
    const SLboolean req[] = {SL_BOOLEAN_TRUE};
    result = (*engineEngine)->CreateAudioPlayer(engineEngine, &playerObject, &ds, &audioSink,
                                                sizeof(ids) / sizeof(SLInterfaceID)/*表示参数个数*/, ids,
                                                req);
    ASSERT_EQ(SL_RESULT_SUCCESS, result);
    LOGD("create AudioPlayer success");

    result = (*playerObject)->Realize(playerObject, SL_BOOLEAN_FALSE);
    ASSERT_EQ(SL_RESULT_SUCCESS, result);
    // 获取播放接口
    result = (*playerObject)->GetInterface(playerObject, SL_IID_PLAY, &playerPlay);
    ASSERT_EQ(SL_RESULT_SUCCESS, result);
    // 获取队列接口
    result = (*playerObject)->GetInterface(playerObject, SL_IID_BUFFERQUEUE, &playerBufferQueue);
    ASSERT_EQ(SL_RESULT_SUCCESS, result);
    // 注册回调函数
    result = (*playerBufferQueue)->RegisterCallback(playerBufferQueue, playerCallback,
                                                    (void *) (path1));
    ASSERT_EQ(SL_RESULT_SUCCESS, result);
    // 设置播放状态
    (*playerPlay)->SetPlayState(playerPlay, SL_PLAYSTATE_PLAYING);
    // 启动回调队列
    (*playerBufferQueue)->Enqueue(playerBufferQueue, "", 1);
    //env->ReleaseStringUTFChars(path_, path);
}

//定点着色器glsl
#define GET_STR(x) #x //将传入的x直接转换为字符串且加了引号，比较清晰
static const char *vertexShader = GET_STR(
        attribute vec4 aPosition;//顶点坐标？
        attribute vec2 aTexCoord;//材质顶点坐标
        varying vec2 vTexCoord;//输出材质坐标,输出给片元着色器
        void main() {
            vTexCoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y);//转换成LCD显示坐标，即原点在左上角
            gl_Position = aPosition;
        }
);//参考ijkplay

extern "C"
JNIEXPORT void JNICALL
Java_com_example_androidffmpegtest_XPlay_Open(JNIEnv *env, jobject instance, jstring url_,
                                              jobject surface) {
    const char *url = env->GetStringUTFChars(url_, 0);

    // TODO
    //1.获取原始窗口
    ANativeWindow *nwin = ANativeWindow_fromSurface(env, surface);

    // 创建EGL
    // 1.create display
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        LOGE("get egldisplay failed");
        return;
    }
    if (EGL_TRUE != eglInitialize(display, 0, 0)) {
        LOGE("egl initialize failed");
        return;
    }

    // 2.create surface
    // 2.1 surface配置，surface可以理解为窗口
    EGLConfig config;//下面函数的输出
    EGLint confignum;
    EGLint configSpec[] = {
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE
    };//输入
    eglChooseConfig(display, configSpec, &config, 1, &confignum);//1表示最多存储1个配置项
    // 2.2 create surface
    EGLSurface winsurface = eglCreateWindowSurface(display, config, nwin, NULL);
    if (winsurface == EGL_NO_SURFACE) {
        LOGE("egl surface initialize failed");
        return;
    }

    // 3.create context
    const EGLint ctxAttr[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE
    };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                                          ctxAttr);//第个参数表示多个设备共享上下文，这里用不到
    if (context == EGL_NO_CONTEXT) {
        LOGE("egl context initialize failed");
        return;
    }

    if (EGL_TRUE != eglMakeCurrent(display, winsurface, winsurface, context)) {
        //保证opengl函数和egl关联起来
        LOGE("egl eglMakeCurrent failed");
        return;
    }
    LOGD("EGL init success");
    env->ReleaseStringUTFChars(url_, url);
}