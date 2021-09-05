#include <time.h>

#include <iostream>
#include <queue>

extern "C" {
#include <X11/Xlib.h>

#include "libavcodec/avcodec.h"
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libswscale/swscale.h"
#include "unistd.h"
}

using namespace std;

AVFormatContext *avFmtCtx = NULL;
AVCodecContext *avRawCodecCtx = NULL;
AVCodecContext *avEncoderCtx;
AVDictionary *avRawOptions = NULL;
AVCodec *avDecodec = NULL;
AVCodec *avEncodec = NULL;
struct SwsContext *swsCtx = NULL;
AVPacket *avRawPkt = NULL;
int videoIndex = -1;
AVFrame *avYUVFrame = NULL;
int width;
int height;

typedef struct {
    int width;
    int height;
    int offset_x;
    int offset_y;
    int screen_number;
} X11parameters;

void initScreenSource(X11parameters x11pmt, bool fullscreen) {
    avdevice_register_all();

    avFmtCtx = avformat_alloc_context();
    avRawPkt = (AVPacket *)malloc(sizeof(AVPacket));
    char *displayName = getenv("DISPLAY");

    if (fullscreen) {
        x11pmt.offset_x = 0;
        x11pmt.offset_y = 0;
        Display *display = XOpenDisplay(displayName);
        int screenNum = DefaultScreen(display);
        width = DisplayWidth(display, screenNum);
        height = DisplayHeight(display, screenNum);
        x11pmt.width = width;
        x11pmt.height = height;
        XCloseDisplay(display);
    } else {
        height = x11pmt.height;
        width = x11pmt.width;
    }

    if (width % 32 != 0) {
        width = width / 32 * 32;
    }
    if (height % 2 != 0) {
        height = height / 2 * 2;
    }

    x11pmt.height = height;
    x11pmt.width = width;

    av_dict_set(&avRawOptions, "video_size", (to_string(x11pmt.width) + "*" + to_string(x11pmt.height)).c_str(), 0);
    av_dict_set(&avRawOptions, "framerate", "30", 0);
    av_dict_set(&avRawOptions, "probesize", "30M", 0);
    AVInputFormat *avInputFmt = av_find_input_format("x11grab");

    if (avInputFmt == NULL) {
        printf("av_find_input_format not found......\n");
        exit(-1);
    }

    if (avformat_open_input(&avFmtCtx, (string(displayName) + "." + to_string(x11pmt.screen_number) + "+" + to_string(x11pmt.offset_x) + "," + to_string(x11pmt.offset_y)).c_str(), avInputFmt, &avRawOptions) != 0) {
        printf("Couldn't open input stream.\n");
        exit(-1);
    }

    if (avformat_find_stream_info(avFmtCtx, &avRawOptions) < 0) {
        printf("Couldn't find stream information.\n");
        exit(-1);
    }

    for (int i = 0; i < avFmtCtx->nb_streams; ++i) {
        if (avFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoIndex = i;
            break;
        }
    }

    if (videoIndex == -1 || videoIndex >= avFmtCtx->nb_streams) {
        printf("Didn't find a video stream.\n");
        exit(-1);
    }

    //deprecato: avRawCodecCtx = avFmtCtx->streams[videoIndex]->codecpar;
    avRawCodecCtx = avcodec_alloc_context3(NULL);
    avcodec_parameters_to_context(avRawCodecCtx, avFmtCtx->streams[videoIndex]->codecpar);

    avDecodec = avcodec_find_decoder(avRawCodecCtx->codec_id);
    if (avDecodec == NULL) {
        printf("Codec not found.\n");
        exit(-1);
    }
    if (avcodec_open2(avRawCodecCtx, avDecodec, NULL) < 0) {
        printf("Could not open decodec . \n");
        exit(-1);
    }

    swsCtx = sws_getContext(avRawCodecCtx->width,
                            avRawCodecCtx->height,
                            avRawCodecCtx->pix_fmt,
                            avRawCodecCtx->width,
                            avRawCodecCtx->height,
                            AV_PIX_FMT_YUV420P,
                            SWS_BICUBIC, NULL, NULL, NULL);

    avYUVFrame = av_frame_alloc();
    //deprecata: int yuvLen = avpicture_get_size(AV_PIX_FMT_YUV420P, avRawCodecCtx->width, avRawCodecCtx->height);
    int yuvLen = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, avRawCodecCtx->width, avRawCodecCtx->height, 1);

    uint8_t *yuvBuf = new uint8_t[yuvLen];
    //deprecata: avpicture_fill((AVPicture *)avYUVFrame, (uint8_t *)yuvBuf, AV_PIX_FMT_YUV420P, avRawCodecCtx->width, avRawCodecCtx->height);
    av_image_fill_arrays(avYUVFrame->data, avYUVFrame->linesize, (uint8_t *)yuvBuf, AV_PIX_FMT_YUV420P, avRawCodecCtx->width, avRawCodecCtx->height, 1);

    avYUVFrame->width = avRawCodecCtx->width;
    avYUVFrame->height = avRawCodecCtx->height;
    avYUVFrame->format = AV_PIX_FMT_YUV420P;

    avEncodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!avEncodec) {
        printf("h264 codec not found\n");
        exit(-1);
    }

    avEncoderCtx = avcodec_alloc_context3(avEncodec);

    avEncoderCtx->codec_id = AV_CODEC_ID_H264;
    avEncoderCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    avEncoderCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    avEncoderCtx->width = x11pmt.width;
    avEncoderCtx->height = x11pmt.height;
    avEncoderCtx->time_base.num = 1;
    avEncoderCtx->time_base.den = 30;
    avEncoderCtx->bit_rate = 128 * 1024 * 8;
    avEncoderCtx->gop_size = 250;
    avEncoderCtx->qmin = 1;
    avEncoderCtx->qmax = 10;

    AVDictionary *param = 0;
    //H.264
    //av_dict_set(&param, "preset", "slow", 0);
    av_dict_set(&param, "preset", "superfast", 0);
    av_dict_set(&param, "tune", "zerolatency", 0);  //实现实时编码

    if (avcodec_open2(avEncoderCtx, avEncodec, &param) < 0) {
        printf("Failed to open video encoder!\n");
        exit(-1);
    }
}

void captureStart(int frameNumber) {
    int got_picture = 0;
    int ret = 0;
    int flag = 0;
    int bufLen = 0;
    uint8_t *outBuf = NULL;

    bufLen = width * height * 3;
    outBuf = (uint8_t *)malloc(bufLen);

    AVFrame *avOutFrame;
    avOutFrame = av_frame_alloc();
    //deprecato: avpicture_fill((AVPicture *)avOutFrame, (uint8_t *)outBuf, avEncoderCtx->pix_fmt, avEncoderCtx->width, avEncoderCtx->height);

    av_image_fill_arrays(avOutFrame->data, avOutFrame->linesize, (uint8_t *)outBuf, avEncoderCtx->pix_fmt, avEncoderCtx->width, avEncoderCtx->height, 1);

    AVPacket pkt;  // 用来存储编码后数据
    memset(&pkt, 0, sizeof(AVPacket));
    av_init_packet(&pkt);

    unsigned int pkSn = 0;
    FILE *h264Fp = fopen("out.264", "wb");

    int start_clock = clock();
    int readframe_clock = 0;
    int readframe_clock_start = 0;
    int readframe_clock_end = 0;
    int decode_clock = 0;
    int decode_clock_start = 0;
    int decode_clock_end = 0;
    int encode_clock = 0;
    int encode_clock_start = 0;
    int encode_clock_end = 0;

    int i = 0;
    while (i < frameNumber) {
        readframe_clock_start = clock();
        if (av_read_frame(avFmtCtx, avRawPkt) >= 0) {
            readframe_clock_end = clock();
            readframe_clock += (readframe_clock_end - readframe_clock_start);
            if (avRawPkt->stream_index == videoIndex) {
                //Inizio DECODING
                //deprecato: flag = avcodec_decode_video2(avRawCodecCtx, avOutFrame, &got_picture, avRawPkt);
                decode_clock_start = clock();
                flag = avcodec_send_packet(avRawCodecCtx, avRawPkt);
                if (flag < 0) {
                    printf("Errore Decoding");
                    break;
                }
                got_picture = avcodec_receive_frame(avRawCodecCtx, avOutFrame);
                decode_clock_end = clock();
                decode_clock += (decode_clock_end - decode_clock_start);
                //Fine DECODING
                if (got_picture == 0) {
                    sws_scale(swsCtx, avOutFrame->data, avOutFrame->linesize, 0, avRawCodecCtx->height, avYUVFrame->data, avYUVFrame->linesize);

                    got_picture = -1;  //forse non serve
                    //Inizio ENCODING
                    //deprecato: flag = avcodec_encode_video2(avEncoderCtx, &pkt, avYUVFrame, &got_picture);
                    encode_clock_start = clock();
                    flag = avcodec_send_frame(avEncoderCtx, avYUVFrame);
                    got_picture = avcodec_receive_packet(avEncoderCtx, &pkt);
                    encode_clock_end = clock();
                    encode_clock += (encode_clock_end - encode_clock_start);
                    //Fine ENCODING

                    if (flag >= 0) {
                        if (got_picture == 0) {
                            int w = fwrite(pkt.data, 1, pkt.size, h264Fp);
                            cout << "Captured " << ++i << " frames" << endl;
                        }
                    }
                    av_packet_unref(&pkt);
                    av_packet_unref(avRawPkt);
                }
            }
        }
    }
    fclose(h264Fp);
    cout << "Finished" << endl
         << endl;
    int total_clock = readframe_clock + encode_clock + decode_clock;
    cout << "ReadFrame clock: " << readframe_clock << " " << (float)100 * readframe_clock / total_clock << "%" << endl;
    cout << "Decode clock: " << decode_clock << " " << (float)100 * decode_clock / total_clock << "%" << endl;
    cout << "Encode clock: " << encode_clock << " " << (float)100 * encode_clock / total_clock << "%" << endl;
}

int main(int argc, char const *argv[]) {
    X11parameters x11pmt;
    x11pmt.width = 300;
    x11pmt.height = 400;
    x11pmt.offset_x = 0;
    x11pmt.offset_y = 0;
    x11pmt.screen_number = 0;
    initScreenSource(x11pmt, false);
    captureStart(30 * 180);
    return 0;
}