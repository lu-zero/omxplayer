/*
 *      Copyright (C) 2005-2008 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "OMXAudioCodecOMX.h"
#ifdef TARGET_LINUX
#include "XMemUtils.h"
#endif
#include "utils/log.h"

COMXAudioCodecOMX::COMXAudioCodecOMX()
{
  m_pBufferOutput = NULL;
  m_iBufferOutputAlloced = 0;

  m_pCodecContext = NULL;
  m_pConvert = NULL;
  m_bOpenedCodec = false;

  m_channelMap[0] = PCM_INVALID;
  m_channels = 0;
  m_layout = 0;
  m_pFrame1 = NULL;
  m_bGotFrame = false;
  m_iSampleFormat = AV_SAMPLE_FMT_NONE;
  m_desiredSampleFormat = AV_SAMPLE_FMT_NONE;
}

COMXAudioCodecOMX::~COMXAudioCodecOMX()
{
  m_dllAvUtil.av_free(m_pBufferOutput);
  m_pBufferOutput = NULL;
  m_iBufferOutputAlloced = 0;
  Dispose();
}

bool COMXAudioCodecOMX::Open(COMXStreamInfo &hints)
{
  AVCodec* pCodec;
  m_bOpenedCodec = false;

  if (!m_dllAvUtil.Load() || !m_dllAvCodec.Load() || !m_dllSwResample.Load())
    return false;

  m_dllAvCodec.avcodec_register_all();

  pCodec = m_dllAvCodec.avcodec_find_decoder(hints.codec);
  if (!pCodec)
  {
    CLog::Log(LOGDEBUG,"COMXAudioCodecOMX::Open() Unable to find codec %d", hints.codec);
    return false;
  }

  m_bFirstFrame = true;
  m_pCodecContext = m_dllAvCodec.avcodec_alloc_context3(pCodec);
  m_pCodecContext->debug_mv = 0;
  m_pCodecContext->debug = 0;
  m_pCodecContext->workaround_bugs = 1;

  if (pCodec->capabilities & CODEC_CAP_TRUNCATED)
    m_pCodecContext->flags |= CODEC_FLAG_TRUNCATED;

  m_channels = 0;
  m_pCodecContext->channels = hints.channels;
  m_pCodecContext->sample_rate = hints.samplerate;
  m_pCodecContext->block_align = hints.blockalign;
  m_pCodecContext->bit_rate = hints.bitrate;
  m_pCodecContext->bits_per_coded_sample = hints.bitspersample;

  if(m_pCodecContext->bits_per_coded_sample == 0)
    m_pCodecContext->bits_per_coded_sample = 16;

  if( hints.extradata && hints.extrasize > 0 )
  {
    m_pCodecContext->extradata_size = hints.extrasize;
    m_pCodecContext->extradata = (uint8_t*)m_dllAvUtil.av_mallocz(hints.extrasize + FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(m_pCodecContext->extradata, hints.extradata, hints.extrasize);
  }

  if (m_dllAvCodec.avcodec_open2(m_pCodecContext, pCodec, NULL) < 0)
  {
    CLog::Log(LOGDEBUG,"COMXAudioCodecOMX::Open() Unable to open codec");
    Dispose();
    return false;
  }

  m_pFrame1 = m_dllAvCodec.avcodec_alloc_frame();
  m_bOpenedCodec = true;
  m_iSampleFormat = AV_SAMPLE_FMT_NONE;
  m_desiredSampleFormat = m_pCodecContext->sample_fmt == AV_SAMPLE_FMT_S16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLTP;
  return true;
}

void COMXAudioCodecOMX::Dispose()
{
  if (m_pFrame1) m_dllAvUtil.av_free(m_pFrame1);
    m_pFrame1 = NULL;

  if (m_pConvert)
    m_dllSwResample.swr_free(&m_pConvert);

  if (m_pCodecContext)
  {
    if (m_pCodecContext->extradata) m_dllAvUtil.av_free(m_pCodecContext->extradata);
    m_pCodecContext->extradata = NULL;
    if (m_bOpenedCodec) m_dllAvCodec.avcodec_close(m_pCodecContext);
    m_bOpenedCodec = false;
    m_dllAvUtil.av_free(m_pCodecContext);
    m_pCodecContext = NULL;
  }

  m_dllAvCodec.Unload();
  m_dllAvUtil.Unload();
  m_dllSwResample.Unload();

  m_bGotFrame = false;
}

int COMXAudioCodecOMX::Decode(BYTE* pData, int iSize)
{
  int iBytesUsed, got_frame;
  if (!m_pCodecContext) return -1;

  AVPacket avpkt;
  m_bGotFrame = false;
  m_dllAvCodec.av_init_packet(&avpkt);
  avpkt.data = pData;
  avpkt.size = iSize;
  iBytesUsed = m_dllAvCodec.avcodec_decode_audio4( m_pCodecContext
                                                 , m_pFrame1
                                                 , &got_frame
                                                 , &avpkt);
  if (iBytesUsed < 0 || !got_frame)
  {
    return iBytesUsed;
  }
  /* some codecs will attempt to consume more data than what we gave */
  if (iBytesUsed > iSize)
  {
    CLog::Log(LOGWARNING, "COMXAudioCodecOMX::Decode - decoder attempted to consume more data than given");
    iBytesUsed = iSize;
  }

  if (m_bFirstFrame)
  {
    CLog::Log(LOGDEBUG, "COMXAudioCodecOMX::Decode(%p,%d) format=%d(%d) chan=%d samples=%d size=%d data=%p,%p,%p,%p,%p,%p,%p,%p",
             pData, iSize, m_pCodecContext->sample_fmt, m_desiredSampleFormat, m_pCodecContext->channels, m_pFrame1->nb_samples,
             m_pFrame1->linesize[0],
             m_pFrame1->data[0], m_pFrame1->data[1], m_pFrame1->data[2], m_pFrame1->data[3], m_pFrame1->data[4], m_pFrame1->data[5], m_pFrame1->data[6], m_pFrame1->data[7]
             );
  }

  m_bGotFrame = true;
  return iBytesUsed;
}

int COMXAudioCodecOMX::GetData(BYTE** dst)
{
  if (!m_bGotFrame)
    return 0;
  int inLineSize, outLineSize;
  /* input audio is aligned */
  int inputSize = m_dllAvUtil.av_samples_get_buffer_size(&inLineSize, m_pCodecContext->channels, m_pFrame1->nb_samples, m_pCodecContext->sample_fmt, 0);
  /* output audio will be packed */
  int outputSize = m_dllAvUtil.av_samples_get_buffer_size(&outLineSize, m_pCodecContext->channels, m_pFrame1->nb_samples, m_desiredSampleFormat, 1);
  bool cont = !m_pFrame1->data[1] || (m_pFrame1->data[1] == m_pFrame1->data[0] + inLineSize && inLineSize == outLineSize && inLineSize * m_pCodecContext->channels == inputSize);

  if (m_iBufferOutputAlloced < outputSize)
  {
     m_dllAvUtil.av_free(m_pBufferOutput);
     m_pBufferOutput = (BYTE*)m_dllAvUtil.av_malloc(outputSize + FF_INPUT_BUFFER_PADDING_SIZE);
     m_iBufferOutputAlloced = outputSize;
  }
  *dst = m_pBufferOutput;

  /* need to convert format */
  if(m_pCodecContext->sample_fmt != m_desiredSampleFormat)
  {
    if(m_pConvert && (m_pCodecContext->sample_fmt != m_iSampleFormat || m_channels != m_pCodecContext->channels))
      m_dllSwResample.swr_free(&m_pConvert);

    if(!m_pConvert)
    {
      m_iSampleFormat = m_pCodecContext->sample_fmt;
      m_pConvert = m_dllSwResample.swr_alloc_set_opts(NULL,
                      m_dllAvUtil.av_get_default_channel_layout(m_pCodecContext->channels),
                      m_desiredSampleFormat, m_pCodecContext->sample_rate,
                      m_dllAvUtil.av_get_default_channel_layout(m_pCodecContext->channels),
                      m_pCodecContext->sample_fmt, m_pCodecContext->sample_rate,
                      0, NULL);

    if(!m_pConvert || m_dllSwResample.swr_init(m_pConvert) < 0)
    {
        CLog::Log(LOGERROR, "COMXAudioCodecOMX::Decode - Unable to initialise convert format %d to %d", m_pCodecContext->sample_fmt, m_desiredSampleFormat);
        return 0;
      }
    }

    /* use unaligned flag to keep output packed */
    uint8_t *out_planes[m_pCodecContext->channels];
    if(m_dllAvUtil.av_samples_fill_arrays(out_planes, NULL, m_pBufferOutput, m_pCodecContext->channels, m_pFrame1->nb_samples, m_desiredSampleFormat, 1) < 0 ||
       m_dllSwResample.swr_convert(m_pConvert, out_planes, m_pFrame1->nb_samples, (const uint8_t **)m_pFrame1->data, m_pFrame1->nb_samples) < 0)
    {
      CLog::Log(LOGERROR, "COMXAudioCodecOMX::Decode - Unable to convert format %d to %d", (int)m_pCodecContext->sample_fmt, m_desiredSampleFormat);
      outputSize = 0;
    }
  }
  else
  {
    /* if it is already contiguous, just return decoded frame */
    if (cont)
    {
      *dst = m_pFrame1->data[0];
    }
    else
    {
      /* copy to a contiguous buffer */
      uint8_t *out_planes[m_pCodecContext->channels];
      if (m_dllAvUtil.av_samples_fill_arrays(out_planes, NULL, m_pBufferOutput, m_pCodecContext->channels, m_pFrame1->nb_samples, m_desiredSampleFormat, 1) < 0 ||
        m_dllAvUtil.av_samples_copy(out_planes, m_pFrame1->data, 0, 0, m_pFrame1->nb_samples, m_pCodecContext->channels, m_desiredSampleFormat) < 0 )
      {
        outputSize = 0;
    }
  }
  }
  if (m_bFirstFrame)
  {
    CLog::Log(LOGDEBUG, "COMXAudioCodecOMX::GetData size=%d/%d line=%d/%d cont=%d buf=%p", inputSize, outputSize, inLineSize, outLineSize, cont, *dst);
    m_bFirstFrame = false;
  }
  return outputSize;
}

void COMXAudioCodecOMX::Reset()
{
  if (m_pCodecContext) m_dllAvCodec.avcodec_flush_buffers(m_pCodecContext);
  m_bGotFrame = false;
}

int COMXAudioCodecOMX::GetChannels()
{
  if (!m_pCodecContext)
    return 0;
  return m_pCodecContext->channels;
}

int COMXAudioCodecOMX::GetSampleRate()
{
  if (!m_pCodecContext)
    return 0;
  return m_pCodecContext->sample_rate;
}

int COMXAudioCodecOMX::GetBitsPerSample()
{
  if (!m_pCodecContext)
    return 0;
  return m_pCodecContext->sample_fmt == AV_SAMPLE_FMT_S16 ? 16 : 32;
}

int COMXAudioCodecOMX::GetBitRate()
{
  if (!m_pCodecContext)
    return 0;
  return m_pCodecContext->bit_rate;
}

static unsigned count_bits(int64_t value)
{
  unsigned bits = 0;
  for(;value;++bits)
    value &= value - 1;
  return bits;
}

void COMXAudioCodecOMX::BuildChannelMap()
{
  if (m_channels == m_pCodecContext->channels && m_layout == m_pCodecContext->channel_layout)
    return; //nothing to do here

  m_channels = m_pCodecContext->channels;
  m_layout   = m_pCodecContext->channel_layout;

  int index = 0;
  if(m_pCodecContext->codec_id == AV_CODEC_ID_AAC && m_pCodecContext->channels == 3)
  {
    m_channelMap[index++] = PCM_FRONT_CENTER;
    m_channelMap[index++] = PCM_FRONT_LEFT;
    m_channelMap[index++] = PCM_FRONT_RIGHT;
  }
  else if(m_pCodecContext->codec_id == AV_CODEC_ID_AAC && m_pCodecContext->channels == 4)
  {
    m_channelMap[index++] = PCM_FRONT_CENTER;
    m_channelMap[index++] = PCM_FRONT_LEFT;
    m_channelMap[index++] = PCM_FRONT_RIGHT;
    m_channelMap[index++] = PCM_BACK_CENTER;
  }
  else if(m_pCodecContext->codec_id == AV_CODEC_ID_AAC && m_pCodecContext->channels == 5)
  {
    m_channelMap[index++] = PCM_FRONT_CENTER;
    m_channelMap[index++] = PCM_FRONT_LEFT;
    m_channelMap[index++] = PCM_FRONT_RIGHT;
    m_channelMap[index++] = PCM_BACK_LEFT;
    m_channelMap[index++] = PCM_BACK_RIGHT;
  }
  else if(m_pCodecContext->codec_id == AV_CODEC_ID_AAC && m_pCodecContext->channels == 6)
  {
    m_channelMap[index++] = PCM_FRONT_CENTER;
    m_channelMap[index++] = PCM_FRONT_LEFT;
    m_channelMap[index++] = PCM_FRONT_RIGHT;
    m_channelMap[index++] = PCM_BACK_LEFT;
    m_channelMap[index++] = PCM_BACK_RIGHT;
    m_channelMap[index++] = PCM_LOW_FREQUENCY;
  }
  else if(m_pCodecContext->codec_id == AV_CODEC_ID_AAC && m_pCodecContext->channels == 7)
  {
    m_channelMap[index++] = PCM_FRONT_CENTER;
    m_channelMap[index++] = PCM_FRONT_LEFT;
    m_channelMap[index++] = PCM_FRONT_RIGHT;
    m_channelMap[index++] = PCM_BACK_LEFT;
    m_channelMap[index++] = PCM_BACK_RIGHT;
    m_channelMap[index++] = PCM_BACK_CENTER;
    m_channelMap[index++] = PCM_LOW_FREQUENCY;
  }
  else if(m_pCodecContext->codec_id == AV_CODEC_ID_AAC && m_pCodecContext->channels == 8)
  {
    m_channelMap[index++] = PCM_FRONT_CENTER;
    m_channelMap[index++] = PCM_SIDE_LEFT;
    m_channelMap[index++] = PCM_SIDE_RIGHT;
    m_channelMap[index++] = PCM_FRONT_LEFT;
    m_channelMap[index++] = PCM_FRONT_RIGHT;
    m_channelMap[index++] = PCM_BACK_LEFT;
    m_channelMap[index++] = PCM_BACK_RIGHT;
    m_channelMap[index++] = PCM_LOW_FREQUENCY;
  }
  else
  {

    int64_t layout;
    int bits = count_bits(m_pCodecContext->channel_layout);
    if (bits == m_pCodecContext->channels)
      layout = m_pCodecContext->channel_layout;
    else
    {
      CLog::Log(LOGINFO, "COMXAudioCodecOMX::GetChannelMap - FFmpeg reported %d channels, but the layout contains %d ignoring", m_pCodecContext->channels, bits);
      layout = m_dllAvUtil.av_get_default_channel_layout(m_pCodecContext->channels);
    }

    if (layout & AV_CH_FRONT_LEFT           ) m_channelMap[index++] = PCM_FRONT_LEFT           ;
    if (layout & AV_CH_FRONT_RIGHT          ) m_channelMap[index++] = PCM_FRONT_RIGHT          ;
    if (layout & AV_CH_FRONT_CENTER         ) m_channelMap[index++] = PCM_FRONT_CENTER         ;
    if (layout & AV_CH_LOW_FREQUENCY        ) m_channelMap[index++] = PCM_LOW_FREQUENCY        ;
    if (layout & AV_CH_BACK_LEFT            ) m_channelMap[index++] = PCM_BACK_LEFT            ;
    if (layout & AV_CH_BACK_RIGHT           ) m_channelMap[index++] = PCM_BACK_RIGHT           ;
    if (layout & AV_CH_FRONT_LEFT_OF_CENTER ) m_channelMap[index++] = PCM_FRONT_LEFT_OF_CENTER ;
    if (layout & AV_CH_FRONT_RIGHT_OF_CENTER) m_channelMap[index++] = PCM_FRONT_RIGHT_OF_CENTER;
    if (layout & AV_CH_BACK_CENTER          ) m_channelMap[index++] = PCM_BACK_CENTER          ;
    if (layout & AV_CH_SIDE_LEFT            ) m_channelMap[index++] = PCM_SIDE_LEFT            ;
    if (layout & AV_CH_SIDE_RIGHT           ) m_channelMap[index++] = PCM_SIDE_RIGHT           ;
    if (layout & AV_CH_TOP_CENTER           ) m_channelMap[index++] = PCM_TOP_CENTER           ;
    if (layout & AV_CH_TOP_FRONT_LEFT       ) m_channelMap[index++] = PCM_TOP_FRONT_LEFT       ;
    if (layout & AV_CH_TOP_FRONT_CENTER     ) m_channelMap[index++] = PCM_TOP_FRONT_CENTER     ;
    if (layout & AV_CH_TOP_FRONT_RIGHT      ) m_channelMap[index++] = PCM_TOP_FRONT_RIGHT      ;
    if (layout & AV_CH_TOP_BACK_LEFT        ) m_channelMap[index++] = PCM_TOP_BACK_LEFT        ;
    if (layout & AV_CH_TOP_BACK_CENTER      ) m_channelMap[index++] = PCM_TOP_BACK_CENTER      ;
    if (layout & AV_CH_TOP_BACK_RIGHT       ) m_channelMap[index++] = PCM_TOP_BACK_RIGHT       ;
  }
}

enum PCMChannels* COMXAudioCodecOMX::GetChannelMap()
{
  BuildChannelMap();

  if (m_channelMap[0] == PCM_INVALID)
    return NULL;

  return m_channelMap;
}
