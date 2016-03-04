// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BASE_MOCK_FILTERS_H_
#define MEDIA_BASE_MOCK_FILTERS_H_

#include <stdint.h>

#include <string>

#include "base/callback.h"
#include "base/macros.h"
#include "media/base/audio_decoder.h"
#include "media/base/audio_decoder_config.h"
#include "media/base/audio_renderer.h"
#include "media/base/cdm_context.h"
#include "media/base/decoder_buffer.h"
#include "media/base/decryptor.h"
#include "media/base/demuxer.h"
#include "media/base/pipeline.h"
#include "media/base/pipeline_status.h"
#include "media/base/renderer.h"
#include "media/base/text_track.h"
#include "media/base/time_source.h"
#include "media/base/video_decoder.h"
#include "media/base/video_decoder_config.h"
#include "media/base/video_frame.h"
#include "media/base/video_renderer.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace media {

class MockPipeline : public Pipeline {
 public:
  MockPipeline();
  virtual ~MockPipeline();

  // Note: Start() and Resume() declarations are not actually overrides; they
  // take scoped_ptr* instead of scoped_ptr so that they can be mock methods.
  // Private stubs for Start() and Resume() implement the actual Pipeline
  // interface by forwarding to these mock methods.
  MOCK_METHOD10(Start,
                void(Demuxer*,
                     scoped_ptr<Renderer>*,
                     const base::Closure&,
                     const PipelineStatusCB&,
                     const PipelineStatusCB&,
                     const PipelineMetadataCB&,
                     const BufferingStateCB&,
                     const base::Closure&,
                     const AddTextTrackCB&,
                     const base::Closure&));
  MOCK_METHOD1(Stop, void(const base::Closure&));
  MOCK_METHOD2(Seek, void(base::TimeDelta, const PipelineStatusCB&));
  MOCK_METHOD1(Suspend, void(const PipelineStatusCB&));
  MOCK_METHOD3(Resume,
               void(scoped_ptr<Renderer>*,
                    base::TimeDelta,
                    const PipelineStatusCB&));

  // TODO(sandersd): This should automatically return true between Start() and
  // Stop(). (Or better, remove it from the interface entirely.)
  MOCK_CONST_METHOD0(IsRunning, bool());

  // TODO(sandersd): These should be regular getters/setters.
  MOCK_CONST_METHOD0(GetPlaybackRate, double());
  MOCK_METHOD1(SetPlaybackRate, void(double));
  MOCK_CONST_METHOD0(GetVolume, float());
  MOCK_METHOD1(SetVolume, void(float));

  // TODO(sandersd): These should probably have setters too.
  MOCK_CONST_METHOD0(GetMediaTime, base::TimeDelta());
  MOCK_CONST_METHOD0(GetBufferedTimeRanges, Ranges<base::TimeDelta>());
  MOCK_CONST_METHOD0(GetMediaDuration, base::TimeDelta());
  MOCK_METHOD0(DidLoadingProgress, bool());
  MOCK_CONST_METHOD0(GetStatistics, PipelineStatistics());

  MOCK_METHOD2(SetCdm, void(CdmContext*, const CdmAttachedCB&));

 private:
  // Forwarding stubs (see comment above).
  void Start(Demuxer* demuxer,
             scoped_ptr<Renderer> renderer,
             const base::Closure& ended_cb,
             const PipelineStatusCB& error_cb,
             const PipelineStatusCB& seek_cb,
             const PipelineMetadataCB& metadata_cb,
             const BufferingStateCB& buffering_state_cb,
             const base::Closure& duration_change_cb,
             const AddTextTrackCB& add_text_track_cb,
             const base::Closure& waiting_for_decryption_key_cb) override;
  void Resume(scoped_ptr<Renderer> renderer,
              base::TimeDelta timestamp,
              const PipelineStatusCB& seek_cb) override;

  DISALLOW_COPY_AND_ASSIGN(MockPipeline);
};

class MockDemuxer : public Demuxer {
 public:
  MockDemuxer();
  virtual ~MockDemuxer();

  // Demuxer implementation.
  virtual std::string GetDisplayName() const;
  MOCK_METHOD3(Initialize,
               void(DemuxerHost* host, const PipelineStatusCB& cb, bool));
  MOCK_METHOD1(SetPlaybackRate, void(double playback_rate));
  MOCK_METHOD2(Seek, void(base::TimeDelta time, const PipelineStatusCB& cb));
  MOCK_METHOD0(Stop, void());
  MOCK_METHOD0(OnAudioRendererDisabled, void());
  MOCK_METHOD1(GetStream, DemuxerStream*(DemuxerStream::Type));
  MOCK_CONST_METHOD0(GetStartTime, base::TimeDelta());
  MOCK_CONST_METHOD0(GetTimelineOffset, base::Time());
  MOCK_CONST_METHOD0(GetMemoryUsage, int64_t());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockDemuxer);
};

class MockDemuxerStream : public DemuxerStream {
 public:
  explicit MockDemuxerStream(DemuxerStream::Type type);
  virtual ~MockDemuxerStream();

  // DemuxerStream implementation.
  Type type() const override;
  Liveness liveness() const override;
  MOCK_METHOD1(Read, void(const ReadCB& read_cb));
  AudioDecoderConfig audio_decoder_config() override;
  VideoDecoderConfig video_decoder_config() override;
  MOCK_METHOD0(EnableBitstreamConverter, void());
  MOCK_METHOD0(SupportsConfigChanges, bool());

  void set_audio_decoder_config(const AudioDecoderConfig& config);
  void set_video_decoder_config(const VideoDecoderConfig& config);
  void set_liveness(Liveness liveness);

  VideoRotation video_rotation() override;

 private:
  Type type_;
  Liveness liveness_;
  AudioDecoderConfig audio_decoder_config_;
  VideoDecoderConfig video_decoder_config_;

  DISALLOW_COPY_AND_ASSIGN(MockDemuxerStream);
};

class MockVideoDecoder : public VideoDecoder {
 public:
  MockVideoDecoder();
  virtual ~MockVideoDecoder();

  // VideoDecoder implementation.
  virtual std::string GetDisplayName() const;
  MOCK_METHOD5(Initialize,
               void(const VideoDecoderConfig& config,
                    bool low_delay,
                    CdmContext* cdm_context,
                    const InitCB& init_cb,
                    const OutputCB& output_cb));
  MOCK_METHOD2(Decode, void(const scoped_refptr<DecoderBuffer>& buffer,
                            const DecodeCB&));
  MOCK_METHOD1(Reset, void(const base::Closure&));
  MOCK_CONST_METHOD0(HasAlpha, bool());
  MOCK_CONST_METHOD0(CanReadWithoutStalling, bool());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockVideoDecoder);
};

class MockAudioDecoder : public AudioDecoder {
 public:
  MockAudioDecoder();
  virtual ~MockAudioDecoder();

  // AudioDecoder implementation.
  virtual std::string GetDisplayName() const;
  MOCK_METHOD4(Initialize,
               void(const AudioDecoderConfig& config,
                    CdmContext* cdm_context,
                    const InitCB& init_cb,
                    const OutputCB& output_cb));
  MOCK_METHOD2(Decode,
               void(const scoped_refptr<DecoderBuffer>& buffer,
                    const DecodeCB&));
  MOCK_METHOD1(Reset, void(const base::Closure&));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockAudioDecoder);
};

class MockVideoRenderer : public VideoRenderer {
 public:
  MockVideoRenderer();
  virtual ~MockVideoRenderer();

  // VideoRenderer implementation.
  MOCK_METHOD9(Initialize,
               void(DemuxerStream* stream,
                    const PipelineStatusCB& init_cb,
                    CdmContext* cdm_context,
                    const StatisticsCB& statistics_cb,
                    const BufferingStateCB& buffering_state_cb,
                    const base::Closure& ended_cb,
                    const PipelineStatusCB& error_cb,
                    const TimeSource::WallClockTimeCB& wall_clock_time_cb,
                    const base::Closure& waiting_for_decryption_key_cb));
  MOCK_METHOD1(Flush, void(const base::Closure& callback));
  MOCK_METHOD1(StartPlayingFrom, void(base::TimeDelta));
  MOCK_METHOD1(OnTimeStateChanged, void(bool));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockVideoRenderer);
};

class MockAudioRenderer : public AudioRenderer {
 public:
  MockAudioRenderer();
  virtual ~MockAudioRenderer();

  // AudioRenderer implementation.
  MOCK_METHOD8(Initialize,
               void(DemuxerStream* stream,
                    const PipelineStatusCB& init_cb,
                    CdmContext* cdm_context,
                    const StatisticsCB& statistics_cb,
                    const BufferingStateCB& buffering_state_cb,
                    const base::Closure& ended_cb,
                    const PipelineStatusCB& error_cb,
                    const base::Closure& waiting_for_decryption_key_cb));
  MOCK_METHOD0(GetTimeSource, TimeSource*());
  MOCK_METHOD1(Flush, void(const base::Closure& callback));
  MOCK_METHOD0(StartPlaying, void());
  MOCK_METHOD1(SetVolume, void(float volume));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockAudioRenderer);
};

class MockRenderer : public Renderer {
 public:
  MockRenderer();
  virtual ~MockRenderer();

  // Renderer implementation.
  MOCK_METHOD7(Initialize,
               void(DemuxerStreamProvider* demuxer_stream_provider,
                    const PipelineStatusCB& init_cb,
                    const StatisticsCB& statistics_cb,
                    const BufferingStateCB& buffering_state_cb,
                    const base::Closure& ended_cb,
                    const PipelineStatusCB& error_cb,
                    const base::Closure& waiting_for_decryption_key_cb));
  MOCK_METHOD1(Flush, void(const base::Closure& flush_cb));
  MOCK_METHOD1(StartPlayingFrom, void(base::TimeDelta timestamp));
  MOCK_METHOD1(SetPlaybackRate, void(double playback_rate));
  MOCK_METHOD1(SetVolume, void(float volume));
  MOCK_METHOD0(GetMediaTime, base::TimeDelta());
  MOCK_METHOD0(HasAudio, bool());
  MOCK_METHOD0(HasVideo, bool());
  MOCK_METHOD2(SetCdm,
               void(CdmContext* cdm_context,
                    const CdmAttachedCB& cdm_attached_cb));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockRenderer);
};

class MockTimeSource : public TimeSource {
 public:
  MockTimeSource();
  virtual ~MockTimeSource();

  // TimeSource implementation.
  MOCK_METHOD0(StartTicking, void());
  MOCK_METHOD0(StopTicking, void());
  MOCK_METHOD1(SetPlaybackRate, void(double));
  MOCK_METHOD1(SetMediaTime, void(base::TimeDelta));
  MOCK_METHOD0(CurrentMediaTime, base::TimeDelta());
  MOCK_METHOD2(GetWallClockTimes,
               bool(const std::vector<base::TimeDelta>&,
                    std::vector<base::TimeTicks>*));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockTimeSource);
};

class MockTextTrack : public TextTrack {
 public:
  MockTextTrack();
  virtual ~MockTextTrack();

  MOCK_METHOD5(addWebVTTCue, void(const base::TimeDelta& start,
                                  const base::TimeDelta& end,
                                  const std::string& id,
                                  const std::string& content,
                                  const std::string& settings));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockTextTrack);
};

class MockDecryptor : public Decryptor {
 public:
  MockDecryptor();
  virtual ~MockDecryptor();

  MOCK_METHOD2(RegisterNewKeyCB, void(StreamType stream_type,
                                      const NewKeyCB& new_key_cb));
  MOCK_METHOD3(Decrypt, void(StreamType stream_type,
                             const scoped_refptr<DecoderBuffer>& encrypted,
                             const DecryptCB& decrypt_cb));
  MOCK_METHOD1(CancelDecrypt, void(StreamType stream_type));
  MOCK_METHOD2(InitializeAudioDecoder,
               void(const AudioDecoderConfig& config,
                    const DecoderInitCB& init_cb));
  MOCK_METHOD2(InitializeVideoDecoder,
               void(const VideoDecoderConfig& config,
                    const DecoderInitCB& init_cb));
  MOCK_METHOD2(DecryptAndDecodeAudio,
               void(const scoped_refptr<media::DecoderBuffer>& encrypted,
                    const AudioDecodeCB& audio_decode_cb));
  MOCK_METHOD2(DecryptAndDecodeVideo,
               void(const scoped_refptr<media::DecoderBuffer>& encrypted,
                    const VideoDecodeCB& video_decode_cb));
  MOCK_METHOD1(ResetDecoder, void(StreamType stream_type));
  MOCK_METHOD1(DeinitializeDecoder, void(StreamType stream_type));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockDecryptor);
};

class MockCdmContext : public CdmContext {
 public:
  MockCdmContext();
  ~MockCdmContext() override;

  MOCK_METHOD0(GetDecryptor, Decryptor*());
  int GetCdmId() const override;

 private:
  DISALLOW_COPY_AND_ASSIGN(MockCdmContext);
};

}  // namespace media

#endif  // MEDIA_BASE_MOCK_FILTERS_H_
