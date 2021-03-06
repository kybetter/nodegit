#include "../include/async_baton.h"

namespace nodegit {
  void deleteBaton(AsyncBaton *baton) {
    delete baton;
  }

  AsyncBaton::AsyncBaton()
    : asyncResource(ThreadPool::GetCurrentAsyncResource()), completedMutex(new std::mutex), hasCompleted(false)
  {}

  void AsyncBaton::SignalCompletion() {
    std::lock_guard<std::mutex> lock(*completedMutex);
    hasCompleted = true;
    completedCondition.notify_one();
  }

  void AsyncBaton::Done() {
    onCompletion();
  }

  Nan::AsyncResource *AsyncBaton::GetAsyncResource() {
    return asyncResource;
  }

  void AsyncBaton::ExecuteAsyncPerform(AsyncCallback asyncCallback, AsyncCallback asyncCancelCb, CompletionCallback onCompletion) {
    auto jsCallback = [asyncCallback, this]() {
      asyncCallback(this);
    };
    auto cancelCallback = [asyncCancelCb, this]() {
      asyncCancelCb(this);
    };

    if (onCompletion) {
      this->onCompletion = [this, onCompletion]() {
        onCompletion(this);
      };

      ThreadPool::PostCallbackEvent(
        [jsCallback, cancelCallback](
          ThreadPool::QueueCallbackFn queueCallback,
          ThreadPool::Callback callbackCompleted
        ) -> ThreadPool::Callback {
          queueCallback(jsCallback, cancelCallback);
          callbackCompleted();

          return []() {};
        }
      );
    } else {
      ThreadPool::PostCallbackEvent(
        [this, jsCallback, cancelCallback](
          ThreadPool::QueueCallbackFn queueCallback,
          ThreadPool::Callback callbackCompleted
        ) -> ThreadPool::Callback {
          this->onCompletion = callbackCompleted;

          queueCallback(jsCallback, cancelCallback);

          return std::bind(&AsyncBaton::SignalCompletion, this);
        }
      );

      WaitForCompletion();
    }
  }

  void AsyncBaton::WaitForCompletion() {
    std::unique_lock<std::mutex> lock(*completedMutex);
    while (!hasCompleted) completedCondition.wait(lock);
  }
}
