#include "video.hpp"
#ifndef INT64_C
#define INT64_C(c) (c ## LL)
#define UINT64_C(c) (c ## ULL)
#endif
extern "C"
{
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libswscale/swscale.h>
}
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <memory>
#include <iomanip>
#include <limits>
#include <cassert>
using namespace std;

static bool fileExists(const string &name) 
{
  return ifstream(name).good();
}

namespace
{
struct Frame
{
  Frame(std::string fileName):
    file(fileName)
  {
    if (!file.is_open())
      throw runtime_error(string("file ") + fileName + " is not open ");
  }
  vector<int> data;
  int count;
  ofstream file;
};
}

static const int Fps = 24;

void Video::read()
{
  cout << "Reading video: " << fileName_ << endl;
  av_register_all();
  AVFormatContext *formatContext;

  formatContext = NULL;
  int len = avformat_open_input(&formatContext, fileName_.c_str(), nullptr, nullptr);

  if (len != 0) 
  {
    cerr << "Could not open input " << fileName_ << endl;;
    throw -0x10;
  }
    
  if (avformat_find_stream_info(formatContext, NULL) < 0) 
  {
    cerr << "Could not read stream information from " <<  fileName_ << endl;
    throw -0x11;
  }
  av_dump_format(formatContext, 0, fileName_.c_str(), 0);

  int audioStreamIndex = -1;
  int videoStreamIndex = -1;

  for (unsigned i = 0; i < formatContext->nb_streams; ++i)
    if (formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
    {
      if (audioStreamIndex == -1)
        audioStreamIndex = i;
    }
    else if (formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
    {
      if (videoStreamIndex == -1)
        videoStreamIndex = i;
    }
  if (audioStreamIndex == -1)
  {
    cerr << "File does not have audio stream" << endl;
    throw -0x34;
  }
  if (videoStreamIndex == -1)
  {
    cerr << "File does not have video stream" << endl;
    throw -0x34;
  }

  auto codec = formatContext->streams[audioStreamIndex]->codec;
  AVCodecContext *audioDecodec;
  {
    if(codec->codec_id == 0)
    {
      cerr << "-0x30" << endl;
      throw -0x30;
    }
    AVCodec* c = avcodec_find_decoder(codec->codec_id);
    if (c == NULL)
    {
      cerr << "Could not find decoder ID " << codec->codec_id << endl;
      throw -0x31;
    }
    audioDecodec = avcodec_alloc_context3(c);
    if (audioDecodec == NULL)
    {
      cerr << "Could not alloc context for decoder " << c->name << endl;
      throw -0x32;
    }
    avcodec_copy_context(audioDecodec, codec);
    int ret = avcodec_open2(audioDecodec, c, NULL);
    if (ret < 0)
    {
      cerr << "Could not open stream decoder " << c->name;
      throw -0x33;
    }
  }
  codec = formatContext->streams[videoStreamIndex]->codec;
  AVCodecContext *videoDecodec;
  {
    if(codec->codec_id == 0)
    {
      cerr << "-0x30" << endl;
      throw -0x30;
    }
    AVCodec* c = avcodec_find_decoder(codec->codec_id);
    if (c == NULL)
    {
      cerr << "Could not find decoder ID " << codec->codec_id << endl;
      throw -0x31;
    }
    videoDecodec = avcodec_alloc_context3(c);
    if (videoDecodec == NULL)
    {
      cerr << "Could not alloc context for decoder " << c->name << endl;
      throw -0x32;
    }
    avcodec_copy_context(videoDecodec, codec);
    int ret = avcodec_open2(videoDecodec, c, NULL);
    if (ret < 0)
    {
      cerr << "Could not open stream decoder " << c->name;
      throw -0x33;
    }
  }
  sampleRate_ = audioDecodec->sample_rate;
  const auto channels = audioDecodec->channels;
  std::cout << "channels: " << channels << std::endl;
  switch (audioDecodec->sample_fmt)
  {
  case AV_SAMPLE_FMT_NONE:
    std::cout << "sample_fmt: AV_SAMPLE_FMT_NONE" << std::endl;
    break;
  case AV_SAMPLE_FMT_U8:
    std::cout << "sample_fmt: U8" << std::endl;
    break;
  case AV_SAMPLE_FMT_S16:
    std::cout << "sample_fmt: S16" << std::endl;
    break;
  case AV_SAMPLE_FMT_S32:
    std::cout << "sample_fmt: S32" << std::endl;
    break;
  case AV_SAMPLE_FMT_FLT:
    std::cout << "sample_fmt: FLT" << std::endl;
    break;
  case AV_SAMPLE_FMT_DBL:
    std::cout << "sample_fmt: DBL" << std::endl;
    break;
  case AV_SAMPLE_FMT_U8P:
    std::cout << "sample_fmt: U8P" << std::endl;
    break;
  case AV_SAMPLE_FMT_S16P:
    std::cout << "sample_fmt: S16P" << std::endl;
    break;
  case AV_SAMPLE_FMT_S32P:
    std::cout << "sample_fmt: S32P" << std::endl;
    break;
  case AV_SAMPLE_FMT_FLTP:
    std::cout << "sample_fmt: FLTP" << std::endl;
    break;
  case AV_SAMPLE_FMT_DBLP:
    std::cout << "sample_fmt: DBLP" << std::endl;
    break;
  case AV_SAMPLE_FMT_NB:
    std::cout << "sample_fmt: NB" << std::endl;
    break;
  }
  AVPacket packet;
  width_ = videoDecodec->width;
  height_ = videoDecodec->height;
  thumbHeight_ = 128;
  thumbWidth_ = videoDecodec->width * thumbHeight_ / videoDecodec->height;
  struct SwsContext *swsContext = sws_getContext(videoDecodec->width, videoDecodec->height, videoDecodec->pix_fmt, 
                                                 thumbWidth_, thumbHeight_, AV_PIX_FMT_RGB24,
                                                 SWS_BICUBIC, NULL, NULL, NULL);
  if (swsContext == NULL) 
  {
    ostringstream err;
    err << "Could not create swscale context for " << videoDecodec->width << "x" << videoDecodec->height;
    throw runtime_error(err.str());
  }

  AVFrame *rgbFrame = av_frame_alloc();
  if (!rgbFrame)
    throw runtime_error("Could not allocate memory for RGB frame");
  rgbFrame->width = thumbWidth();
  rgbFrame->height = thumbHeight();
  rgbFrame->format = AV_PIX_FMT_RGB24;
  auto numBytes = avpicture_get_size((AVPixelFormat)rgbFrame->format, rgbFrame->width, rgbFrame->height);
  vector<shared_ptr<Frame> > levels;
  uint8_t *buffer = (uint8_t *)av_malloc(numBytes);
  avpicture_fill((AVPicture *)rgbFrame, buffer, (AVPixelFormat)rgbFrame->format, rgbFrame->width, rgbFrame->height);
  thumbLinesize_ = rgbFrame->linesize[0];
  bool isThumbCached = fileExists(fileName_ + ".thum0");
  bool firstAudioFrame = true;
  while (av_read_frame(formatContext, &packet) == 0)
  {
    if (packet.stream_index == audioStreamIndex)
    {
      if (firstAudioFrame)
      {
        firstAudioFrame = false;
        const auto c = packet.pts * audioDecodec->time_base.num * audioDecodec->sample_rate / audioDecodec->time_base.den;
        for (int i = 0; i < c; ++i)
          audio_.push_back(0);
      }
      int gotFrame = 0;
      AVFrame *decodedFrame = av_frame_alloc();
      int len = avcodec_decode_audio4(audioDecodec, decodedFrame, &gotFrame, &packet);
      if (len >= 0)
      {
        if (gotFrame)
        {
          int dataSize = av_samples_get_buffer_size(nullptr, channels,
                                                    decodedFrame->nb_samples,
                                                    audioDecodec->sample_fmt, 1);
          for (size_t i = 0; i < dataSize / sizeof(float) / channels; ++i)
          {
            int sum = 0;
            for (int c = 0; c < channels; ++c)
              sum += ((float *)decodedFrame->data[0])[i * channels + c] * 0x8000;
            audio_.push_back(sum / channels);
          }
        }
      }
      av_free(decodedFrame);
    }
    else if (packet.stream_index == videoStreamIndex && !isThumbCached)
    {
      if (packet.pts % (Fps * 10) == 0)
        clog << setfill('0') << setw(2) << packet.pts / Fps / 60 << ":"  << setw(2) << packet.pts / Fps % 60 << "."  << setw(2) << packet.pts % Fps << endl;
      AVFrame *decodedFrame = av_frame_alloc();
      int result;
      avcodec_decode_video2(videoDecodec, decodedFrame, &result, &packet);
      if (result)
      {
        sws_scale(swsContext, decodedFrame->data, decodedFrame->linesize, 0, decodedFrame->height, 
                  rgbFrame->data, rgbFrame->linesize);
        int level = 0;
        while (level >= static_cast<int>(levels.size()))
        {
          shared_ptr<Frame> f = make_shared<Frame>(fileName_ + ".thum" + to_string(levels.size()));
          levels.push_back(f);
          f->data.resize(rgbFrame->linesize[0] * rgbFrame->height);
          for (auto &i: f->data)
            i = 0;
          f->count = 0;
        }
        auto f = levels[level];
        vector<int> d;
        for (auto i = rgbFrame->data[0]; i < rgbFrame->data[0] + rgbFrame->linesize[0] * rgbFrame->height; ++i)
          d.push_back(*i);
        f->file.write((const char *)&d[0], d.size() * sizeof(d[0]));
        for (size_t i = 0; i < f->data.size(); ++i)
          f->data[i] += rgbFrame->data[0][i];
        ++f->count;
        while (f->count > 1)
        {
          ++level;
          while (level >= static_cast<int>(levels.size()))
          {
            shared_ptr<Frame> f = make_shared<Frame>(fileName_ + ".thum" + to_string(levels.size()));
            levels.push_back(f);
            f->data.resize(rgbFrame->linesize[0] * rgbFrame->height);
            for (auto &i: f->data)
              i = 0;
            f->count = 0;
          }
          auto f2 = levels[level];
          f2->file.write((const char *)&f->data[0], f->data.size() * sizeof(f->data[0]));
          for (size_t i = 0; i < f2->data.size(); ++i)
          {
            f2->data[i] += f->data[i];
            f->data[i] = 0;
          }
          ++f2->count;
          f->count = 0;
          f = f2;
        }
      }
      av_free(decodedFrame);
    }
    av_free_packet(&packet);
  }
  av_free(buffer);
  av_free(rgbFrame);
  sws_freeContext(swsContext);
  avcodec_close(audioDecodec);
  av_free(audioDecodec);
  avformat_free_context(formatContext);

  if (!fileExists(fileName_ + ".pcs"))
  {
    minMaxCalc();
  }
  else
  {
    ifstream f(fileName_ + ".pcs");
    int n = audio_.size() / 2;
    while (n > 0)
    {
      minMax_.push_back(vector<pair<int16_t, int16_t> >(n + 1));
      f.read((char *)&minMax_.back()[0], minMax_.back().size() * sizeof(minMax_.back()[0]));
      n /= 2;
    }
  }
}

void Video::minMaxCalc()
{
  minMax_.clear();
  int n = audio_.size() / 2;
  const auto mm = make_pair(numeric_limits<int16_t>::max(), numeric_limits<int16_t>::min());
  while (n > 0)
  {
    minMax_.push_back(vector<pair<int16_t, int16_t> >(n + 1));
    for (auto &i: minMax_.back())
      i = mm;
    n /= 2;
  }
  size_t per = 0;
  for (size_t i = 0; i < audio_.size(); ++i)
  {
    auto v = audio_[i];
    for (size_t j = 0; j < minMax_.size(); ++j)
    {
      assert(i / (1 << (j + 1)) < minMax_[j].size());
      auto &z = minMax_[j][i / (1 << (j + 1))];
      z = make_pair(min(z.first, v), max(z.second, v));
    }
    if (i >= per)
    {
      cout << i * 100 / audio_.size() << endl;
      per += audio_.size() / 100;
    }
  }
  ofstream f(fileName_ + ".pcs");
  for (const auto &i: minMax_)
    f.write((const char *)&i[0], i.size() * sizeof(i[0]));
}

int Video::thumbHeight() const
{
  return thumbHeight_;
}

int Video::thumbWidth() const
{
  return thumbWidth_;
}
