// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_MOJO_SERVICES_MOJO_MEDIA_APPLICATION_H_
#define MEDIA_MOJO_SERVICES_MOJO_MEDIA_APPLICATION_H_

#include <stdint.h>

#include <memory>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "media/mojo/interfaces/service_factory.mojom.h"
#include "media/mojo/services/media_mojo_export.h"
#include "services/shell/public/cpp/interface_factory.h"
#include "services/shell/public/cpp/service.h"
#include "services/shell/public/cpp/service_context_ref.h"
#include "url/gurl.h"

namespace media {

class MediaLog;
class MojoMediaClient;

class MEDIA_MOJO_EXPORT MojoMediaApplication
    : public NON_EXPORTED_BASE(shell::Service),
      public NON_EXPORTED_BASE(shell::InterfaceFactory<mojom::ServiceFactory>) {
 public:
  MojoMediaApplication(std::unique_ptr<MojoMediaClient> mojo_media_client,
                       const base::Closure& quit_closure);
  ~MojoMediaApplication() final;

 private:
  // shell::Service implementation.
  void OnStart(const shell::Identity& identity) final;
  bool OnConnect(shell::Connection* connection) final;
  bool OnStop() final;

  // shell::InterfaceFactory<mojom::ServiceFactory> implementation.
  void Create(const shell::Identity& remote_identity,
              mojo::InterfaceRequest<mojom::ServiceFactory> request) final;

  // Note: Since each instance runs on a different thread, do not share a common
  // MojoMediaClient with other instances to avoid threading issues. Hence using
  // a unique_ptr here.
  std::unique_ptr<MojoMediaClient> mojo_media_client_;

  shell::mojom::InterfaceProvider* remote_interface_provider_ = nullptr;
  scoped_refptr<MediaLog> media_log_;
  shell::ServiceContextRefFactory ref_factory_;
};

}  // namespace media

#endif  // MEDIA_MOJO_SERVICES_MOJO_MEDIA_APPLICATION_H_
