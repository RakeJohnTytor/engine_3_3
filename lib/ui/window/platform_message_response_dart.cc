// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/lib/ui/window/platform_message_response_dart.h"

#include <utility>

#include "flutter/common/task_runners.h"
#include "flutter/fml/make_copyable.h"
#include "flutter/fml/trace_event.h"
#include "third_party/tonic/dart_state.h"
#include "third_party/tonic/logging/dart_invoke.h"
#include "third_party/tonic/typed_data/dart_byte_data.h"

static std::atomic<uint64_t> platform_message_counter = 1;

namespace flutter {
namespace {
template <typename Callback, typename TaskRunner, typename Result>
void PostCompletion(Callback&& callback,
                    const TaskRunner& ui_task_runner,
                    bool* is_complete,
                    const std::string& channel,
                    Result&& result) {
  if (callback.is_empty()) {
    return;
  }
  FML_DCHECK(!*is_complete);
  *is_complete = true;
  uint64_t platform_message_id = platform_message_counter.fetch_add(1);
  TRACE_EVENT_ASYNC_BEGIN1("flutter", "PlatformChannel ScheduleResult",
                           platform_message_id, "channel", channel.c_str());
  ui_task_runner->PostTask(fml::MakeCopyable(
      [callback = std::move(callback), platform_message_id,
       result = std::move(result), channel = channel]() mutable {
        TRACE_EVENT_ASYNC_END0("flutter", "PlatformChannel ScheduleResult",
                               platform_message_id);
        std::shared_ptr<tonic::DartState> dart_state =
            callback.dart_state().lock();
        if (!dart_state) {
          return;
        }
        tonic::DartState::Scope scope(dart_state);
        tonic::DartInvoke(callback.Release(), {result()});
      }));
}
}  // namespace

PlatformMessageResponseDart::PlatformMessageResponseDart(
    tonic::DartPersistentValue callback,
    fml::RefPtr<fml::TaskRunner> ui_task_runner,
    const std::string& channel)
    : callback_(std::move(callback)),
      ui_task_runner_(std::move(ui_task_runner)),
      channel_(channel) {}

PlatformMessageResponseDart::~PlatformMessageResponseDart() {
  if (!callback_.is_empty()) {
    ui_task_runner_->PostTask(fml::MakeCopyable(
        [callback = std::move(callback_)]() mutable { callback.Clear(); }));
  }
}

void PlatformMessageResponseDart::Complete(std::unique_ptr<fml::Mapping> data) {
  PostCompletion(std::move(callback_), ui_task_runner_, &is_complete_, channel_,
                 [data = std::move(data)] {
                   return tonic::DartByteData::Create(data->GetMapping(),
                                                      data->GetSize());
                 });
}

void PlatformMessageResponseDart::CompleteEmpty() {
  PostCompletion(std::move(callback_), ui_task_runner_, &is_complete_, channel_,
                 [] { return Dart_Null(); });
}

}  // namespace flutter
