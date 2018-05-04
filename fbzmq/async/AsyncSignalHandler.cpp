/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <fbzmq/async/AsyncSignalHandler.h>

#include <sys/signalfd.h>
#include <unistd.h>

#include <folly/Format.h>

namespace fbzmq {

AsyncSignalHandler::AsyncSignalHandler(ZmqEventLoop* evl) : evl_(evl) {
  CHECK(evl_) << "Error: event loop empty";

  if (sigemptyset(&registeredSignals_) < 0) {
    PLOG(FATAL) << "AsyncSignalHandler: Failed to empty a signal mask";
  }

  // Create signal-fd for signal handling
  if ((signalFd_ = signalfd(
           -1 /* create a new fd */,
           &registeredSignals_,
           SFD_CLOEXEC /* flags */)) < 0) {
    PLOG(FATAL) << "AsyncSignalHandler: Failed to create a signalfd.";
  }

  // Attach callback on signal fd
  evl_->addSocketFd(signalFd_, ZMQ_POLLIN, [this](int revents) noexcept {
    CHECK(revents & ZMQ_POLLIN);

    // Receive signal
    struct signalfd_siginfo fdsi;
    auto bytesRead = read(signalFd_, &fdsi, sizeof(fdsi));
    CHECK_EQ(sizeof(fdsi), bytesRead);

    VLOG(1) << "AsyncSignalHandler: Received signal " << fdsi.ssi_signo;
    signalReceived(static_cast<int>(fdsi.ssi_signo));
  });
}

AsyncSignalHandler::~AsyncSignalHandler() {
  close(signalFd_);
}

void
AsyncSignalHandler::registerSignalHandler(int sig) {
  auto ret = sigismember(&registeredSignals_, sig);
  if (ret < 0) {
    PLOG(FATAL) << "AsyncSignalHandler: invalid/unsupported signal number "
                << sig;
  }
  if (ret == 1) {
    // This signal has already been registered
    throw std::runtime_error(
        folly::sformat("handler already registered for signal {}", sig));
  }

  // Block the signal so that it's not handled according to its default
  // disposition
  sigset_t mask;
  if (sigemptyset(&mask) < 0) {
    PLOG(FATAL) << "AsyncSignalHandler: Failed to empty a signal mask";
  }
  if (sigaddset(&mask, sig) < 0) {
    PLOG(FATAL) << "AsyncSignalHandler: Failed to add a signal into a mask";
  }
  if (pthread_sigmask(SIG_BLOCK, &mask, nullptr) != 0) {
    PLOG(FATAL) << "AsyncSignalHandler: Failed to block signals";
  }

  // Update signal-fd
  if (sigaddset(&registeredSignals_, sig) < 0) {
    PLOG(FATAL) << "AsyncSignalHandler: Failed to add a signal into a mask";
  }
  if ((signalFd_ = signalfd(
           signalFd_ /* update fd */,
           &registeredSignals_,
           SFD_CLOEXEC /* flags */)) < 0) {
    PLOG(FATAL) << "AsyncSignalHandler: Failed to update signalfd.";
  }
}

void
AsyncSignalHandler::unregisterSignalHandler(int sig) {
  const auto ret = sigismember(&registeredSignals_, sig);
  if (ret < 0) {
    PLOG(FATAL) << "AsyncSignalHandler: invalid/unsupported signal number "
                << sig;
  }
  if (ret == 0) {
    throw std::runtime_error(folly::sformat(
        "Unable to unregister handler for signal {}. Signal not registered.",
        sig));
  }

  // Unblock a signal
  sigset_t mask;
  if (sigemptyset(&mask) < 0) {
    PLOG(FATAL) << "AsyncSignalHandler: Failed to empty a signal mask";
  }
  if (sigaddset(&mask, sig) < 0) {
    PLOG(FATAL) << "AsyncSignalHandler: Failed to add a signal into a mask";
  }
  if (pthread_sigmask(SIG_UNBLOCK, &mask, nullptr) != 0) {
    PLOG(FATAL) << "AsyncSignalHandler: Failed to unblock signals";
  }

  // Update signal-fd
  if (sigdelset(&registeredSignals_, sig) < 0) {
    PLOG(FATAL) << "AsyncSignalHandler: Failed to delete a signal from a mask";
  }
  if ((signalFd_ = signalfd(
           signalFd_ /* update fd */,
           &registeredSignals_,
           SFD_CLOEXEC /* flags */)) < 0) {
    PLOG(FATAL) << "AsyncSignalHandler: Failed to update signalfd.";
  }
}

ZmqEventLoop*
AsyncSignalHandler::getZmqEventLoop() {
  return evl_;
}

} // namespace fbzmq
