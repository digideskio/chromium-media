// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/mojo/services/mojo_demuxer_stream_adapter.h"

#include <stdint.h>
#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/numerics/safe_conversions.h"
#include "media/base/decoder_buffer.h"
#include "media/mojo/common/media_type_converters.h"
#include "media/mojo/common/mojo_decoder_buffer_converter.h"
#include "mojo/public/cpp/system/data_pipe.h"

namespace media {

MojoDemuxerStreamAdapter::MojoDemuxerStreamAdapter(
    mojom::DemuxerStreamPtr demuxer_stream,
    const base::Closure& stream_ready_cb)
    : demuxer_stream_(std::move(demuxer_stream)),
      stream_ready_cb_(stream_ready_cb),
      type_(UNKNOWN),
      weak_factory_(this) {
  DVLOG(1) << __FUNCTION__;
  demuxer_stream_->Initialize(base::Bind(
      &MojoDemuxerStreamAdapter::OnStreamReady, weak_factory_.GetWeakPtr()));
}

MojoDemuxerStreamAdapter::~MojoDemuxerStreamAdapter() {
  DVLOG(1) << __FUNCTION__;
}

void MojoDemuxerStreamAdapter::Read(const ReadCB& read_cb) {
  DVLOG(3) << __FUNCTION__;
  // We shouldn't be holding on to a previous callback if a new Read() came in.
  DCHECK(read_cb_.is_null());

  read_cb_ = read_cb;
  demuxer_stream_->Read(base::Bind(&MojoDemuxerStreamAdapter::OnBufferReady,
                                   weak_factory_.GetWeakPtr()));
}

AudioDecoderConfig MojoDemuxerStreamAdapter::audio_decoder_config() {
  DCHECK_EQ(type_, AUDIO);
  return audio_config_;
}

VideoDecoderConfig MojoDemuxerStreamAdapter::video_decoder_config() {
  DCHECK_EQ(type_, VIDEO);
  return video_config_;
}

DemuxerStream::Type MojoDemuxerStreamAdapter::type() const {
  return type_;
}

void MojoDemuxerStreamAdapter::EnableBitstreamConverter() {
  demuxer_stream_->EnableBitstreamConverter();
}

bool MojoDemuxerStreamAdapter::SupportsConfigChanges() {
  return true;
}

VideoRotation MojoDemuxerStreamAdapter::video_rotation() {
  NOTIMPLEMENTED();
  return VIDEO_ROTATION_0;
}

bool MojoDemuxerStreamAdapter::enabled() const {
  return true;
}

void MojoDemuxerStreamAdapter::set_enabled(bool enabled,
                                           base::TimeDelta timestamp) {
  NOTIMPLEMENTED();
}

void MojoDemuxerStreamAdapter::SetStreamStatusChangeCB(
    const StreamStatusChangeCB& cb) {
  NOTIMPLEMENTED();
}

// TODO(xhwang): Pass liveness here.
void MojoDemuxerStreamAdapter::OnStreamReady(
    Type type,
    mojo::ScopedDataPipeConsumerHandle consumer_handle,
    mojom::AudioDecoderConfigPtr audio_config,
    mojom::VideoDecoderConfigPtr video_config) {
  DVLOG(1) << __FUNCTION__;
  DCHECK_EQ(UNKNOWN, type_);
  DCHECK(consumer_handle.is_valid());

  type_ = type;

  mojo_decoder_buffer_reader_.reset(
      new MojoDecoderBufferReader(std::move(consumer_handle)));

  UpdateConfig(std::move(audio_config), std::move(video_config));

  stream_ready_cb_.Run();
}

void MojoDemuxerStreamAdapter::OnBufferReady(
    Status status,
    mojom::DecoderBufferPtr buffer,
    mojom::AudioDecoderConfigPtr audio_config,
    mojom::VideoDecoderConfigPtr video_config) {
  DVLOG(3) << __FUNCTION__;
  DCHECK(!read_cb_.is_null());
  DCHECK_NE(type_, UNKNOWN);

  if (status == kConfigChanged) {
    UpdateConfig(std::move(audio_config), std::move(video_config));
    base::ResetAndReturn(&read_cb_).Run(kConfigChanged, nullptr);
    return;
  }

  if (status == kAborted) {
    base::ResetAndReturn(&read_cb_).Run(kAborted, nullptr);
    return;
  }

  DCHECK_EQ(status, kOk);

  scoped_refptr<DecoderBuffer> media_buffer =
      mojo_decoder_buffer_reader_->ReadDecoderBuffer(buffer);
  if (!media_buffer) {
    base::ResetAndReturn(&read_cb_).Run(kAborted, nullptr);
    return;
  }

  base::ResetAndReturn(&read_cb_).Run(kOk, media_buffer);
}

void MojoDemuxerStreamAdapter::UpdateConfig(
    mojom::AudioDecoderConfigPtr audio_config,
    mojom::VideoDecoderConfigPtr video_config) {
  DCHECK_NE(type_, UNKNOWN);

  switch(type_) {
    case AUDIO:
      DCHECK(audio_config && !video_config);
      audio_config_ = audio_config.To<AudioDecoderConfig>();
      break;
    case VIDEO:
      DCHECK(video_config && !audio_config);
      video_config_ = video_config.To<VideoDecoderConfig>();
      break;
    default:
      NOTREACHED();
  }
}

}  // namespace media
