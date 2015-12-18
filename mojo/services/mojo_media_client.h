// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_MOJO_SERVICES_MOJO_MEDIA_CLIENT_H_
#define MEDIA_MOJO_SERVICES_MOJO_MEDIA_CLIENT_H_

#include "base/single_thread_task_runner.h"
#include "media/base/audio_renderer_sink.h"
#include "media/base/cdm_factory.h"
#include "media/base/media_log.h"
#include "media/base/renderer_factory.h"
#include "media/base/video_renderer_sink.h"

namespace mojo {
class ServiceProvider;
}

namespace media {

class MojoMediaClient {
 public:
  virtual ~MojoMediaClient();

  static scoped_ptr<MojoMediaClient> Create();

  // Called exactly once before any other method.
  virtual void Initialize();
  // Returns the RendererFactory to be used by MojoRendererService. If returns
  // null, a RendererImpl will be used with audio/video decoders provided in
  // CreateAudioDecoders() and CreateVideoDecoders().
  virtual scoped_ptr<RendererFactory> CreateRendererFactory(
      const scoped_refptr<MediaLog>& media_log);
  // The output sink used for rendering audio or video respectively.
  virtual scoped_refptr<AudioRendererSink> CreateAudioRendererSink();
  virtual scoped_ptr<VideoRendererSink> CreateVideoRendererSink(
      const scoped_refptr<base::SingleThreadTaskRunner>& task_runner);

  // Returns the CdmFactory to be used by MojoCdmService.
  virtual scoped_ptr<CdmFactory> CreateCdmFactory(
      mojo::ServiceProvider* service_provider);

 protected:
  MojoMediaClient();
};

}  // namespace media

#endif  // MEDIA_MOJO_SERVICES_MOJO_MEDIA_CLIENT_H_
