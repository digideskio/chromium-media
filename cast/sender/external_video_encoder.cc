// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/cast/sender/external_video_encoder.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/scoped_vector.h"
#include "base/memory/shared_memory.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/histogram.h"
#include "media/base/video_frame.h"
#include "media/base/video_util.h"
#include "media/cast/cast_defines.h"
#include "media/cast/logging/logging_defines.h"
#include "media/cast/net/cast_transport_config.h"
#include "media/video/video_encode_accelerator.h"

namespace {

static const size_t kOutputBufferCount = 3;

void LogFrameEncodedEvent(
    const scoped_refptr<media::cast::CastEnvironment>& cast_environment,
    base::TimeTicks event_time,
    media::cast::RtpTimestamp rtp_timestamp,
    uint32 frame_id) {
  cast_environment->Logging()->InsertFrameEvent(
      event_time, media::cast::FRAME_ENCODED, media::cast::VIDEO_EVENT,
      rtp_timestamp, frame_id);
}

}  // namespace

namespace media {
namespace cast {

// Container for the associated data of a video frame being processed.
struct InProgressFrameEncode {
  const RtpTimestamp rtp_timestamp;
  const base::TimeTicks reference_time;
  const VideoEncoder::FrameEncodedCallback frame_encoded_callback;

  InProgressFrameEncode(RtpTimestamp rtp,
                        base::TimeTicks r_time,
                        VideoEncoder::FrameEncodedCallback callback)
      : rtp_timestamp(rtp),
        reference_time(r_time),
        frame_encoded_callback(callback) {}
};

// Owns a VideoEncoderAccelerator instance and provides the necessary adapters
// to encode media::VideoFrames and emit media::cast::EncodedFrames.  All
// methods must be called on the thread associated with the given
// SingleThreadTaskRunner, except for the task_runner() accessor.
class ExternalVideoEncoder::VEAClientImpl
    : public VideoEncodeAccelerator::Client,
      public base::RefCountedThreadSafe<VEAClientImpl> {
 public:
  VEAClientImpl(
      const scoped_refptr<CastEnvironment>& cast_environment,
      const scoped_refptr<base::SingleThreadTaskRunner>& encoder_task_runner,
      scoped_ptr<media::VideoEncodeAccelerator> vea,
      int max_frame_rate,
      const CreateVideoEncodeMemoryCallback& create_video_encode_memory_cb)
      : cast_environment_(cast_environment),
        task_runner_(encoder_task_runner),
        max_frame_rate_(max_frame_rate),
        create_video_encode_memory_cb_(create_video_encode_memory_cb),
        video_encode_accelerator_(vea.Pass()),
        encoder_active_(false),
        last_encoded_frame_id_(kStartFrameId),
        key_frame_encountered_(false) {
  }

  base::SingleThreadTaskRunner* task_runner() const {
    return task_runner_.get();
  }

  void Initialize(const gfx::Size& frame_size,
                  VideoCodecProfile codec_profile,
                  int start_bit_rate,
                  const CastInitializationCallback& initialization_cb) {
    DCHECK(task_runner_->RunsTasksOnCurrentThread());
    DCHECK(!frame_size.IsEmpty());

    encoder_active_ = video_encode_accelerator_->Initialize(
        media::VideoFrame::I420,
        frame_size,
        codec_profile,
        start_bit_rate,
        this);

    UMA_HISTOGRAM_BOOLEAN("Cast.Sender.VideoEncodeAcceleratorInitializeSuccess",
                          encoder_active_);

    if (!initialization_cb.is_null()) {
      cast_environment_->PostTask(
          CastEnvironment::MAIN,
          FROM_HERE,
          base::Bind(initialization_cb,
                     encoder_active_ ? STATUS_VIDEO_INITIALIZED :
                         STATUS_HW_VIDEO_ENCODER_NOT_SUPPORTED));
    }
  }

  void SetBitRate(int bit_rate) {
    DCHECK(task_runner_->RunsTasksOnCurrentThread());

    video_encode_accelerator_->RequestEncodingParametersChange(bit_rate,
                                                               max_frame_rate_);
  }

  void EncodeVideoFrame(
      const scoped_refptr<media::VideoFrame>& video_frame,
      const base::TimeTicks& reference_time,
      bool key_frame_requested,
      const VideoEncoder::FrameEncodedCallback& frame_encoded_callback) {
    DCHECK(task_runner_->RunsTasksOnCurrentThread());

    if (!encoder_active_)
      return;

    in_progress_frame_encodes_.push_back(InProgressFrameEncode(
        TimeDeltaToRtpDelta(video_frame->timestamp(), kVideoFrequency),
        reference_time,
        frame_encoded_callback));

    // BitstreamBufferReady will be called once the encoder is done.
    video_encode_accelerator_->Encode(video_frame, key_frame_requested);
  }

 protected:
  void NotifyError(VideoEncodeAccelerator::Error error) override {
    DCHECK(task_runner_->RunsTasksOnCurrentThread());
    VLOG(1) << "ExternalVideoEncoder NotifyError: " << error;

    encoder_active_ = false;
    // TODO(miu): Plumbing is required to bubble this up to the CastSession and
    // beyond.
    // TODO(miu): Force-flush all |in_progress_frame_encodes_| immediately so
    // pending frames do not become stuck, freezing VideoSender.
  }

  // Called to allocate the input and output buffers.
  void RequireBitstreamBuffers(unsigned int input_count,
                               const gfx::Size& input_coded_size,
                               size_t output_buffer_size) override {
    DCHECK(task_runner_->RunsTasksOnCurrentThread());

    // TODO(miu): Investigate why we are ignoring |input_count| (4) and instead
    // using |kOutputBufferCount| (3) here.
    for (size_t j = 0; j < kOutputBufferCount; ++j) {
      create_video_encode_memory_cb_.Run(
          output_buffer_size,
          base::Bind(&VEAClientImpl::OnCreateSharedMemory, this));
    }
  }

  // Encoder has encoded a frame and it's available in one of the output
  // buffers.  Package the result in a media::cast::EncodedFrame and post it
  // to the Cast MAIN thread via the supplied callback.
  void BitstreamBufferReady(int32 bitstream_buffer_id,
                            size_t payload_size,
                            bool key_frame) override {
    DCHECK(task_runner_->RunsTasksOnCurrentThread());
    if (bitstream_buffer_id < 0 ||
        bitstream_buffer_id >= static_cast<int32>(output_buffers_.size())) {
      NOTREACHED();
      VLOG(1) << "BitstreamBufferReady(): invalid bitstream_buffer_id="
              << bitstream_buffer_id;
      NotifyError(media::VideoEncodeAccelerator::kPlatformFailureError);
      return;
    }
    base::SharedMemory* output_buffer = output_buffers_[bitstream_buffer_id];
    if (payload_size > output_buffer->mapped_size()) {
      NOTREACHED();
      VLOG(1) << "BitstreamBufferReady(): invalid payload_size = "
              << payload_size;
      NotifyError(media::VideoEncodeAccelerator::kPlatformFailureError);
      return;
    }
    if (key_frame)
      key_frame_encountered_ = true;
    if (!key_frame_encountered_) {
      // Do not send video until we have encountered the first key frame.
      // Save the bitstream buffer in |stream_header_| to be sent later along
      // with the first key frame.
      //
      // TODO(miu): Should |stream_header_| be an std::ostringstream for
      // performance reasons?
      stream_header_.append(static_cast<const char*>(output_buffer->memory()),
                            payload_size);
    } else if (!in_progress_frame_encodes_.empty()) {
      const InProgressFrameEncode& request = in_progress_frame_encodes_.front();

      scoped_ptr<EncodedFrame> encoded_frame(new EncodedFrame());
      encoded_frame->dependency = key_frame ? EncodedFrame::KEY :
          EncodedFrame::DEPENDENT;
      encoded_frame->frame_id = ++last_encoded_frame_id_;
      if (key_frame)
        encoded_frame->referenced_frame_id = encoded_frame->frame_id;
      else
        encoded_frame->referenced_frame_id = encoded_frame->frame_id - 1;
      encoded_frame->rtp_timestamp = request.rtp_timestamp;
      encoded_frame->reference_time = request.reference_time;
      if (!stream_header_.empty()) {
        encoded_frame->data = stream_header_;
        stream_header_.clear();
      }
      encoded_frame->data.append(
          static_cast<const char*>(output_buffer->memory()), payload_size);

      cast_environment_->PostTask(
          CastEnvironment::MAIN,
          FROM_HERE,
          base::Bind(&LogFrameEncodedEvent,
                     cast_environment_,
                     cast_environment_->Clock()->NowTicks(),
                     encoded_frame->rtp_timestamp,
                     encoded_frame->frame_id));

      cast_environment_->PostTask(
          CastEnvironment::MAIN,
          FROM_HERE,
          base::Bind(request.frame_encoded_callback,
                     base::Passed(&encoded_frame)));

      in_progress_frame_encodes_.pop_front();
    } else {
      VLOG(1) << "BitstreamBufferReady(): no encoded frame data available";
    }

    // We need to re-add the output buffer to the encoder after we are done
    // with it.
    video_encode_accelerator_->UseOutputBitstreamBuffer(media::BitstreamBuffer(
        bitstream_buffer_id,
        output_buffers_[bitstream_buffer_id]->handle(),
        output_buffers_[bitstream_buffer_id]->mapped_size()));
  }

 private:
  friend class base::RefCountedThreadSafe<VEAClientImpl>;

  ~VEAClientImpl() override {
    // According to the media::VideoEncodeAccelerator interface, Destroy()
    // should be called instead of invoking its private destructor.
    task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&media::VideoEncodeAccelerator::Destroy,
                   base::Unretained(video_encode_accelerator_.release())));
  }

  // Note: This method can be called on any thread.
  void OnCreateSharedMemory(scoped_ptr<base::SharedMemory> memory) {
    task_runner_->PostTask(FROM_HERE,
                           base::Bind(&VEAClientImpl::OnReceivedSharedMemory,
                                      this,
                                      base::Passed(&memory)));
  }

  void OnReceivedSharedMemory(scoped_ptr<base::SharedMemory> memory) {
    DCHECK(task_runner_->RunsTasksOnCurrentThread());

    output_buffers_.push_back(memory.release());

    // Wait until all requested buffers are received.
    if (output_buffers_.size() < kOutputBufferCount)
      return;

    // Immediately provide all output buffers to the VEA.
    for (size_t i = 0; i < output_buffers_.size(); ++i) {
      video_encode_accelerator_->UseOutputBitstreamBuffer(
          media::BitstreamBuffer(static_cast<int32>(i),
                                 output_buffers_[i]->handle(),
                                 output_buffers_[i]->mapped_size()));
    }
  }

  const scoped_refptr<CastEnvironment> cast_environment_;
  const scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  const int max_frame_rate_;
  const CreateVideoEncodeMemoryCallback create_video_encode_memory_cb_;
  scoped_ptr<media::VideoEncodeAccelerator> video_encode_accelerator_;
  bool encoder_active_;
  uint32 last_encoded_frame_id_;
  bool key_frame_encountered_;
  std::string stream_header_;

  // Shared memory buffers for output with the VideoAccelerator.
  ScopedVector<base::SharedMemory> output_buffers_;

  // FIFO list.
  std::list<InProgressFrameEncode> in_progress_frame_encodes_;

  DISALLOW_COPY_AND_ASSIGN(VEAClientImpl);
};

ExternalVideoEncoder::ExternalVideoEncoder(
    const scoped_refptr<CastEnvironment>& cast_environment,
    const VideoSenderConfig& video_config,
    const gfx::Size& frame_size,
    const CastInitializationCallback& initialization_cb,
    const CreateVideoEncodeAcceleratorCallback& create_vea_cb,
    const CreateVideoEncodeMemoryCallback& create_video_encode_memory_cb)
    : cast_environment_(cast_environment),
      create_video_encode_memory_cb_(create_video_encode_memory_cb),
      bit_rate_(video_config.start_bitrate),
      key_frame_requested_(false),
      weak_factory_(this) {
  DCHECK(cast_environment_->CurrentlyOn(CastEnvironment::MAIN));
  DCHECK_GT(video_config.max_frame_rate, 0);
  DCHECK(!frame_size.IsEmpty());
  DCHECK(!create_vea_cb.is_null());
  DCHECK(!create_video_encode_memory_cb_.is_null());
  DCHECK_GT(bit_rate_, 0);

  VideoCodecProfile codec_profile;
  switch (video_config.codec) {
    case CODEC_VIDEO_VP8:
      codec_profile = media::VP8PROFILE_ANY;
      break;
    case CODEC_VIDEO_H264:
      codec_profile = media::H264PROFILE_MAIN;
      break;
    case CODEC_VIDEO_FAKE:
      NOTREACHED() << "Fake software video encoder cannot be external";
      // ...flow through to next case...
    default:
      cast_environment_->PostTask(
          CastEnvironment::MAIN,
          FROM_HERE,
          base::Bind(initialization_cb, STATUS_HW_VIDEO_ENCODER_NOT_SUPPORTED));
      return;
  }

  create_vea_cb.Run(
      base::Bind(&ExternalVideoEncoder::OnCreateVideoEncodeAccelerator,
                 weak_factory_.GetWeakPtr(),
                 frame_size,
                 codec_profile,
                 video_config.max_frame_rate,
                 initialization_cb));
}

ExternalVideoEncoder::~ExternalVideoEncoder() {
}

bool ExternalVideoEncoder::EncodeVideoFrame(
    const scoped_refptr<media::VideoFrame>& video_frame,
    const base::TimeTicks& reference_time,
    const FrameEncodedCallback& frame_encoded_callback) {
  DCHECK(cast_environment_->CurrentlyOn(CastEnvironment::MAIN));
  DCHECK(!video_frame->visible_rect().IsEmpty());
  DCHECK(!frame_encoded_callback.is_null());

  if (!client_)
    return false;  // Not ready.

  client_->task_runner()->PostTask(FROM_HERE,
                                   base::Bind(&VEAClientImpl::EncodeVideoFrame,
                                              client_,
                                              video_frame,
                                              reference_time,
                                              key_frame_requested_,
                                              frame_encoded_callback));
  key_frame_requested_ = false;
  return true;
}

void ExternalVideoEncoder::SetBitRate(int new_bit_rate) {
  DCHECK(cast_environment_->CurrentlyOn(CastEnvironment::MAIN));
  DCHECK_GT(new_bit_rate, 0);

  bit_rate_ = new_bit_rate;
  if (!client_)
    return;
  client_->task_runner()->PostTask(
      FROM_HERE, base::Bind(&VEAClientImpl::SetBitRate, client_, bit_rate_));
}

void ExternalVideoEncoder::GenerateKeyFrame() {
  DCHECK(cast_environment_->CurrentlyOn(CastEnvironment::MAIN));
  key_frame_requested_ = true;
}

void ExternalVideoEncoder::LatestFrameIdToReference(uint32 /*frame_id*/) {
  // Do nothing.  Not supported.
}

void ExternalVideoEncoder::OnCreateVideoEncodeAccelerator(
    const gfx::Size& frame_size,
    VideoCodecProfile codec_profile,
    int max_frame_rate,
    const CastInitializationCallback& initialization_cb,
    scoped_refptr<base::SingleThreadTaskRunner> encoder_task_runner,
    scoped_ptr<media::VideoEncodeAccelerator> vea) {
  DCHECK(cast_environment_->CurrentlyOn(CastEnvironment::MAIN));

  // The callback will be invoked with null pointers in the case where the
  // system does not support or lacks the resources to provide GPU-accelerated
  // video encoding.
  if (!encoder_task_runner || !vea) {
    if (!initialization_cb.is_null()) {
      cast_environment_->PostTask(
          CastEnvironment::MAIN,
          FROM_HERE,
          base::Bind(initialization_cb, STATUS_INVALID_VIDEO_CONFIGURATION));
    }
    return;
  }

  DCHECK(!client_);
  client_ = new VEAClientImpl(cast_environment_,
                              encoder_task_runner,
                              vea.Pass(),
                              max_frame_rate,
                              create_video_encode_memory_cb_);
  client_->task_runner()->PostTask(FROM_HERE,
                                   base::Bind(&VEAClientImpl::Initialize,
                                              client_,
                                              frame_size,
                                              codec_profile,
                                              bit_rate_,
                                              initialization_cb));
}

}  //  namespace cast
}  //  namespace media
