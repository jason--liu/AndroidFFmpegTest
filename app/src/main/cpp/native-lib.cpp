#include <jni.h>
#include <string>
#include <android/log.h>
#include <unistd.h>
#include <string.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

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

//顶点着色器glsl
#define GET_STR(x) #x //将传入的x直接转换为字符串且加了引号，比较清晰
static const char *vertexShader = GET_STR(
        attribute
        vec4 aPosition;//顶点坐标？
        attribute
        vec2 aTexCoord;//材质顶点坐标
        varying
        vec2 vTexCoord;//输出材质坐标,输出给片元着色器
        void main() {
            vTexCoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y);//转换成LCD显示坐标，即原点在左上角
            gl_Position = aPosition;
        }
);//参考ijkplay

// 片元着色器
// p表示平面存储，即Y存完了再存U,V ffmpeg软解码和部分x86硬解码出来的格式
static const char *fragYUV420p = GET_STR(
        precision
        mediump float;//精度
        varying
        vec2 vTexCoord;//顶点着色器传递的坐标
        // 三个输入参数，输入材质（灰度材质，单像素）
        uniform
        sampler2D yTexture;
        uniform
        sampler2D uTexture;
        uniform
        sampler2D vTexture;
        void main() {
            vec3 yuv;
            vec3 rgb;
            yuv.r = texture2D(yTexture, vTexCoord).r;
            yuv.g = texture2D(uTexture, vTexCoord).r - 0.5;
            yuv.b = texture2D(vTexture, vTexCoord).r - 0.5;
            rgb = mat3(1.0, 1.0, 1.0,
                       0.0, -0.39465, 2.03211,
                       1.13983, -0.58060, 0.0) * yuv;
            //输出像素颜色
            gl_FragColor = vec4(rgb, 1.0);
        }
);

GLint InitShader(const char *code, GLint type) {
    // 创建shader
    GLint sh = glCreateShader(type);
    if (!sh) {
        LOGE("glCreateShader faild", type);
        return 0;
    }
    // 加载shader
    glShaderSource(sh,
                   1,//shader数量
                   &code, //shader执行代码
                   0);//第4个参数表示代码长度，0表示直接找字符串结尾
    //编译shader
    glCompileShader(sh);
    //获取编译情况
    GLint status;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &status);
    if (!status) {
        LOGE("glGetShaderiv failed type 0x%04x", type);
        return 0;
    }
    return sh;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_androidffmpegtest_XPlay_Open(JNIEnv *env, jobject instance, jstring url_,
                                              jobject surface) {
    const char *url = env->GetStringUTFChars(url_, 0);

    // TODO
    // 视频源大小
    int width = 424;
    int hight = 240;
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

    // shader初始化
    // 顶点shader初始化
    GLint vsh = InitShader(vertexShader, GL_VERTEX_SHADER);
    // 片元yuv420p shader初始化
    GLint fsh = InitShader(fragYUV420p, GL_FRAGMENT_SHADER);

    //////////////// 创建渲染程序 //////////////
    GLint program = glCreateProgram();
    if (!program) {
        LOGE("glCreateProgram failed");
        return;
    }
    // 到这里就表示程序开始正常运行了
    // 渲染程序中加入着色器代码
    glAttachShader(program, vsh);
    glAttachShader(program, fsh);

    // 链接程序
    glLinkProgram(program);
    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        LOGE("glLink failed");
        return;
    }
    LOGD("glLink success");
    // 激活渲染程序
    glUseProgram(program);
    ////////////////////////////////////////////

    // 加入三维顶点数据 由两个三角形组成正方形
    static float vers[] = {
            1.0f, -1.0f, 0.0f,
            -1.0f, -1.0f, 0.0f,
            1.0f, 1.0f, 0.0f,
            -1.0f, 1.0f, 0.0f,
    };
    GLuint apos = glGetAttribLocation(program, "aPosition");//返回值要转换？
    glEnableVertexAttribArray(apos);
    // 传递顶点坐标
    glVertexAttribPointer(apos, 3, GL_FLOAT, GL_FALSE, 12,
                          vers);//3表示一个点有xyz三个元素，12表示点存储间隔，3个浮点数占3x4=12字节
    // 加入材质坐标数据
    static float txts[] = {
            1.0f, 0.0f,//右下
            0.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 1.0f
    };
    GLuint atex = glGetAttribLocation(program, "aTexCoord");
    glEnableVertexAttribArray(atex);
    glVertexAttribPointer(atex, 2, GL_FLOAT, GL_FALSE, 8, txts);

    // 材质纹理初始化
    // 设置纹理层 将shader和yuv材质绑定？
    glUniform1i(glGetUniformLocation(program, "yTexture"), 0); //对应材质第一层
    glUniform1i(glGetUniformLocation(program, "uTexture"), 1); //对应材质第二层
    glUniform1i(glGetUniformLocation(program, "vTexture"), 2); //对应材质第三层

    // 创建opengl材质
    GLuint texts[3] = {0};
    glGenTextures(3, texts);
    // 设置纹理属性0
    glBindTexture(GL_TEXTURE_2D, texts[0]);
    // 缩小、放大的过滤器 因为视频可能拉伸放大
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);//这个可以理解为设置渲染方法？
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // 设置纹理格式和大小
    glTexImage2D(GL_TEXTURE_2D, 0,//细节基本 0默认
                 GL_LUMINANCE,//gpu内部格式 亮度，灰度图
                 width, hight, //尺寸是2的次方 拉伸到全屏
                 0,//边框
                 GL_LUMINANCE,//数据格式，亮度
                 GL_UNSIGNED_BYTE,// 像素数据类型
                 NULL // 纹理数据，解码后再设置
    );
    // 设置纹理属性1
    glBindTexture(GL_TEXTURE_2D, texts[1]);
    // 缩小、放大的过滤器 因为视频可能拉伸放大
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);//这个可以理解为设置渲染方法？
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // 设置纹理格式和大小
    glTexImage2D(GL_TEXTURE_2D, 0,//细节基本 0默认
                 GL_LUMINANCE,//gpu内部格式 亮度，灰度图
                 width / 2, hight / 2, //尺寸是2的次方 拉伸到全屏
                 0,//边框
                 GL_LUMINANCE,//数据格式，亮度
                 GL_UNSIGNED_BYTE,// 像素数据类型
                 NULL // 纹理数据，解码后再设置
    );
    // 设置纹理属性2
    glBindTexture(GL_TEXTURE_2D, texts[2]);
    // 缩小、放大的过滤器 因为视频可能拉伸放大
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);//这个可以理解为设置渲染方法？
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // 设置纹理格式和大小
    glTexImage2D(GL_TEXTURE_2D, 0,//细节基本 0默认
                 GL_LUMINANCE,//gpu内部格式 亮度，灰度图
                 width / 2, hight / 2, //尺寸是2的次方 拉伸到全屏
                 0,//边框
                 GL_LUMINANCE,//数据格式，亮度
                 GL_UNSIGNED_BYTE,// 像素数据类型
                 NULL // 纹理数据，解码后再设置
    );

    ////////////////////////////纹理修改和显示/////////////////////////
    unsigned char *buf[3] = {0}; // 像素格式转换一定要用unsigned char，char类型有符号位会影响计算
    buf[0] = new unsigned char[width * hight];
    buf[1] = new unsigned char[width * hight / 4];// 宽高都除以2
    buf[2] = new unsigned char[width * hight / 4];

    //测试
    for (int i = 0; i < 10000; i++) {

        memset(buf[0], i, width * hight);
        memset(buf[1], i, width * hight / 4);
        memset(buf[2], i, width * hight / 4);
        // 激活第一层 yTexture 绑定到创建的纹理
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texts[0]);
        //替换纹理内容
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, hight, GL_LUMINANCE,/*灰度图*/
                        GL_UNSIGNED_BYTE/*存储格式*/, buf[0]);

        // 激活第二层 yTexture 绑定到创建的纹理
        glActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, texts[1]);
        //替换纹理内容
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width / 2, hight / 2, GL_LUMINANCE,/*灰度图*/
                        GL_UNSIGNED_BYTE/*存储格式*/, buf[1]);

        // 激活第三层 yTexture 绑定到创建的纹理
        glActiveTexture(GL_TEXTURE0 + 2);
        glBindTexture(GL_TEXTURE_2D, texts[2]);
        //替换纹理内容
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width / 2, hight / 2, GL_LUMINANCE,/*灰度图*/
                        GL_UNSIGNED_BYTE/*存储格式*/, buf[2]);
        // 三维(平面)绘制，所以数据都是绘制在surface中
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);//从0开始共4个顶点
        // 窗口显示
        eglSwapBuffers(display, winsurface);
    }
    env->ReleaseStringUTFChars(url_, url);
}