// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/mojo/services/mojo_audio_decoder_service.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/logging.h"
#include "media/base/cdm_context.h"
#include "media/mojo/common/media_type_converters.h"

namespace media {

MojoAudioDecoderService::MojoAudioDecoderService(
    scoped_ptr<media::AudioDecoder> decoder,
    mojo::InterfaceRequest<interfaces::AudioDecoder> request)
    : binding_(this, std::move(request)),
      decoder_(std::move(decoder)),
      weak_factory_(this) {
  weak_this_ = weak_factory_.GetWeakPtr();
}

MojoAudioDecoderService::~MojoAudioDecoderService() {}

void MojoAudioDecoderService::Initialize(
    interfaces::AudioDecoderClientPtr client,
    interfaces::AudioDecoderConfigPtr config,
    int32_t cdm_id,
    const InitializeCallback& callback) {
  DVLOG(1) << __FUNCTION__ << " "
           << config.To<media::AudioDecoderConfig>().AsHumanReadableString();

  // Encrypted streams are not supported for now.
  if (config.To<media::AudioDecoderConfig>().is_encrypted() &&
      cdm_id == CdmContext::kInvalidCdmId) {
    // The client should prevent this situation.
    NOTREACHED() << "Encrypted streams are not supported";
    callback.Run(false, false);
    return;
  }

  client_ = std::move(client);

  // TODO(timav): Get CdmContext from cdm_id.
  decoder_->Initialize(
      config.To<media::AudioDecoderConfig>(),
      nullptr,  // no CdmContext
      base::Bind(&MojoAudioDecoderService::OnInitialized, weak_this_, callback),
      base::Bind(&MojoAudioDecoderService::OnAudioBufferReady, weak_this_));
}

void MojoAudioDecoderService::SetDataSource(
    mojo::ScopedDataPipeConsumerHandle receive_pipe) {
  DVLOG(1) << __FUNCTION__;
  consumer_handle_ = std::move(receive_pipe);
}

void MojoAudioDecoderService::Decode(interfaces::DecoderBufferPtr buffer,
                                     const DecodeCallback& callback) {
  DVLOG(3) << __FUNCTION__;
  decoder_->Decode(ReadDecoderBuffer(std::move(buffer)),
                   base::Bind(&MojoAudioDecoderService::OnDecodeStatus,
                              weak_this_, callback));
}

void MojoAudioDecoderService::Reset(const ResetCallback& callback) {
  DVLOG(1) << __FUNCTION__;
  decoder_->Reset(
      base::Bind(&MojoAudioDecoderService::OnResetDone, weak_this_, callback));
}

void MojoAudioDecoderService::OnInitialized(const InitializeCallback& callback,
                                            bool success) {
  DVLOG(1) << __FUNCTION__ << " success:" << success;
  callback.Run(success, decoder_->NeedsBitstreamConversion());
}

static interfaces::AudioDecoder::DecodeStatus ConvertDecodeStatus(
    media::AudioDecoder::Status status) {
  switch (status) {
    case media::AudioDecoder::kOk:
      return interfaces::AudioDecoder::DecodeStatus::OK;
    case media::AudioDecoder::kAborted:
      return interfaces::AudioDecoder::DecodeStatus::ABORTED;
    case media::AudioDecoder::kDecodeError:
      return interfaces::AudioDecoder::DecodeStatus::DECODE_ERROR;
  }
  NOTREACHED();
  return interfaces::AudioDecoder::DecodeStatus::DECODE_ERROR;
}

void MojoAudioDecoderService::OnDecodeStatus(
    const DecodeCallback& callback,
    media::AudioDecoder::Status status) {
  DVLOG(3) << __FUNCTION__ << " status:" << status;
  callback.Run(ConvertDecodeStatus(status));
}

void MojoAudioDecoderService::OnResetDone(const ResetCallback& callback) {
  DVLOG(1) << __FUNCTION__;
  callback.Run();
}

void MojoAudioDecoderService::OnAudioBufferReady(
    const scoped_refptr<AudioBuffer>& audio_buffer) {
  DVLOG(1) << __FUNCTION__;

  // TODO(timav): Use DataPipe.
  client_->OnBufferDecoded(interfaces::AudioBuffer::From(audio_buffer));
}

scoped_refptr<DecoderBuffer> MojoAudioDecoderService::ReadDecoderBuffer(
    interfaces::DecoderBufferPtr buffer) {
  scoped_refptr<DecoderBuffer> media_buffer(
      buffer.To<scoped_refptr<DecoderBuffer>>());

  if (media_buffer->end_of_stream())
    return media_buffer;

  // Wait for the data to become available in the DataPipe.
  MojoHandleSignalsState state;
  CHECK_EQ(MOJO_RESULT_OK,
           MojoWait(consumer_handle_.get().value(), MOJO_HANDLE_SIGNAL_READABLE,
                    MOJO_DEADLINE_INDEFINITE, &state));
  CHECK_EQ(MOJO_HANDLE_SIGNAL_READABLE, state.satisfied_signals);

  // Read the inner data for the DecoderBuffer from our DataPipe.
  uint32_t bytes_to_read =
      base::checked_cast<uint32_t>(media_buffer->data_size());
  DCHECK_GT(bytes_to_read, 0u);
  uint32_t bytes_read = bytes_to_read;
  CHECK_EQ(ReadDataRaw(consumer_handle_.get(), media_buffer->writable_data(),
                       &bytes_read, MOJO_READ_DATA_FLAG_ALL_OR_NONE),
           MOJO_RESULT_OK);
  CHECK_EQ(bytes_to_read, bytes_read);

  return media_buffer;
}

}  // namespace media
