// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>

#include "base/time/time.h"
#include "media/cast/cast_config.h"
#include "media/cast/receiver/video_decoder.h"
#include "media/cast/sender/sender_encoded_frame.h"
#include "media/cast/sender/vp8_encoder.h"
#include "media/cast/sender/vp8_quantizer_parser.h"
#include "media/cast/test/utility/default_config.h"
#include "media/cast/test/utility/video_utility.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {
namespace cast {

namespace {
const int kWidth = 320;
const int kHeight = 240;
const int kFrameRate = 10;
const int kQp = 20;

VideoSenderConfig GetVideoConfigForTest() {
  VideoSenderConfig config = GetDefaultVideoSenderConfig();
  config.codec = CODEC_VIDEO_VP8;
  config.use_external_encoder = false;
  config.max_frame_rate = kFrameRate;
  config.min_qp = kQp;
  config.max_qp = kQp;
  return config;
}
}  // unnamed namespace

class Vp8QuantizerParserTest : public ::testing::Test {
 public:
  Vp8QuantizerParserTest() : video_config_(GetVideoConfigForTest()) {}

  // Call vp8 software encoder to encode one randomly generated frame.
  void EncodeOneFrame(SenderEncodedFrame* encoded_frame) {
    const gfx::Size frame_size = gfx::Size(kWidth, kHeight);
    const scoped_refptr<VideoFrame> video_frame = VideoFrame::CreateFrame(
        PIXEL_FORMAT_YV12, frame_size, gfx::Rect(frame_size), frame_size,
        next_frame_timestamp_);
    const base::TimeTicks reference_time =
        base::TimeTicks::UnixEpoch() + next_frame_timestamp_;
    next_frame_timestamp_ += base::TimeDelta::FromSeconds(1) / kFrameRate;
    PopulateVideoFrameWithNoise(video_frame.get());
    vp8_encoder_->Encode(video_frame, reference_time, encoded_frame);
  }

  // Update the vp8 encoder with the new quantizer.
  void UpdateQuantizer(int qp) {
    DCHECK((qp > 3) && (qp < 64));
    video_config_.min_qp = qp;
    video_config_.max_qp = qp;
    RecreateVp8Encoder();
  }

 protected:
  void SetUp() final {
    next_frame_timestamp_ = base::TimeDelta();
    RecreateVp8Encoder();
  }

 private:
  // Reconstruct a vp8 encoder with new config since the Vp8Encoder
  // class has no interface to update the config.
  void RecreateVp8Encoder() {
    vp8_encoder_.reset(new Vp8Encoder(video_config_));
    vp8_encoder_->Initialize();
  }

  base::TimeDelta next_frame_timestamp_;
  VideoSenderConfig video_config_;
  scoped_ptr<Vp8Encoder> vp8_encoder_;

  DISALLOW_COPY_AND_ASSIGN(Vp8QuantizerParserTest);
};

// Encode 5 frames to test the cases with insufficient data input.
TEST_F(Vp8QuantizerParserTest, InsufficientData) {
  for (int i = 0; i < 5; ++i) {
    scoped_ptr<SenderEncodedFrame> encoded_frame(new SenderEncodedFrame());
    const uint8* encoded_data =
        reinterpret_cast<const uint8*>(encoded_frame->data.data());
    // Null input.
    int decoded_quantizer =
        ParseVp8HeaderQuantizer(encoded_data, encoded_frame->data.size());
    EXPECT_EQ(-1, decoded_quantizer);
    EncodeOneFrame(encoded_frame.get());
    encoded_data = reinterpret_cast<const uint8*>(encoded_frame->data.data());
    // Zero bytes should not be enough to decode the quantizer value.
    decoded_quantizer = ParseVp8HeaderQuantizer(encoded_data, 0);
    EXPECT_EQ(-1, decoded_quantizer);
    // Three bytes should not be enough to decode the quantizer value..
    decoded_quantizer = ParseVp8HeaderQuantizer(encoded_data, 3);
    EXPECT_EQ(-1, decoded_quantizer);
    unsigned int first_partition_size =
        (encoded_data[0] | (encoded_data[1] << 8) | (encoded_data[2] << 16)) >>
        5;
    if (encoded_frame->dependency == EncodedFrame::KEY) {
      // Ten bytes should not be enough to decode the quanitizer value
      // for a Key frame.
      decoded_quantizer = ParseVp8HeaderQuantizer(encoded_data, 10);
      EXPECT_EQ(-1, decoded_quantizer);
      // One byte less than needed to decode the quantizer value.
      decoded_quantizer =
          ParseVp8HeaderQuantizer(encoded_data, 10 + first_partition_size - 1);
      EXPECT_EQ(-1, decoded_quantizer);
      // Minimum number of bytes to decode the quantizer value.
      decoded_quantizer =
          ParseVp8HeaderQuantizer(encoded_data, 10 + first_partition_size);
      EXPECT_EQ(kQp, decoded_quantizer);
    } else {
      // One byte less than needed to decode the quantizer value.
      decoded_quantizer =
          ParseVp8HeaderQuantizer(encoded_data, 3 + first_partition_size - 1);
      EXPECT_EQ(-1, decoded_quantizer);
      // Minimum number of bytes to decode the quantizer value.
      decoded_quantizer =
          ParseVp8HeaderQuantizer(encoded_data, 3 + first_partition_size);
      EXPECT_EQ(kQp, decoded_quantizer);
    }
  }
}

// Encode 5 fames for every quantizer value in the range of [4,63].
// crbug.com/537635: disable VariedQuantizer on Thread Sanitizer
#if defined(THREAD_SANITIZER)
# define MAYBE_VariedQuantizer DISABLED_VariedQuantizer
#else
# define MAYBE_VariedQuantizer VariedQuantizer
#endif
TEST_F(Vp8QuantizerParserTest, MAYBE_VariedQuantizer) {
  int decoded_quantizer = -1;
  for (int qp = 4; qp <= 63; ++qp) {
    UpdateQuantizer(qp);
    for (int i = 0; i < 5; ++i) {
      scoped_ptr<SenderEncodedFrame> encoded_frame(new SenderEncodedFrame());
      EncodeOneFrame(encoded_frame.get());
      decoded_quantizer = ParseVp8HeaderQuantizer(
          reinterpret_cast<const uint8*>(encoded_frame->data.data()),
          encoded_frame->data.size());
      EXPECT_EQ(qp, decoded_quantizer);
    }
  }
}

}  // namespace cast
}  // namespace media
