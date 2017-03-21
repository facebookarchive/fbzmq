# fbzmq
`fbzmq` provides a framework for writing services in C++ while leveraging
awesomeness of `libzmq` (message passing semantics). At a high level it provides
- Lightweight C++ wrapper over `libzmq` which leverages newer C++ constructs
  and stricter type checking. Most notably it provides the ability to
  send/receive `thrift objects` as messages over wire without worrying about
  wire encoding/decoding protocols.
- Powerful `Async Framework` with EventLoop, Timeouts, SignalHandler and more to
  enable developers write asynchronous applications efficiently.

## Examples
Here is a simple example demonstrating some powerful abstractions of `fbzmq`
which makes writing asynchronous applications easier on top of `libzmq`

```
// Create context
fbzmq::Context context{1};

// Create eventloop
fbzmq::ZmqEventLoop evl;

// Create thrift serializer
apache::thrift::CompactSerializer serializer;

// Create REP and PUB server sockets
fbzmq::Socket<ZMQ_REP, ZMQ_SERVER> reqSocket;
fbzmq::Socket<ZMQ_PUB, ZMQ_SERVER> pubSocket;
reqSocket.bind("tcp://[::1]:12345");
pubSocket.bind("tcp://[::1]:12346");

// Attach callback on reqSocket
evl.addSocket(RawZmqSocketPtr{*reqSocket}, ZMQ_POLLIN, [&](int evts) noexcept {
  // It must be POLLIN event
  CHECK(evts | ZMQ_POLLIN);

  // Read request
  auto request = reqSocket.recvThriftObj<thrift::Request>(serializer);

  // Send response
  thrift::Response response;
  response.request = request;
  response.success = true;
  reqSocket.sendThriftObj(response, serializer);
});

// Create periodic timeout to send publications and schedule it every 10s
auto timeout = fbzmq::ZmqTimeout::make(&evl, [&]() noexcept {
  thrift::User user;
  user.name = "TestPerson";
  user.email = "test@person.com";
  pubSocket.sendThriftObj(nodeData, serializer);
});
timeout->scheduleTimeout(std::chrono::seconds(10), true /* periodic */);

// Let the magic begin
evl.run()

```

## Requirements
We have tried `fbzmq` on `Ubuntu-14.04`, `Ubuntu-16.04` and `CentOS-7`. This
should work on all Linux based platforms without any issues.

* Compiler supporting C++14 or higher
* libzmq-4.0.6 or greater

## Building fbzmq

### Dependencies
* `cmake`
* `gflags`
* `gtest`
* `libsodium`
* `libzmq`
* `zstd`
* `folly`
* `fbthrift`

### Dependencies Installation
`build` directory contains scripts which will help you install all dependencies
on the fresh copy of specified Linux distribution. Once all dependencies are
installed, you can proceed with build step.

If you have a different Linux distribution then you can manually install above
specified dependencies and development tools.

```
cd build

// On Ubuntu 14.04
sudo bash deps_ubuntu_14.04.sh

// On Ubuntu 16.04
sudo bash deps_ubuntu_16.04.sh

// On CentOS
sudo bash deps_centos.sh
```

### Build Steps
```
// Step into `build` directory
cd build

// Generate make files
cmake ..

// Build and run-tests
make
make test
```

## Installing fbzmq
`fbzmq` builds a static library and install steps installs library as well all
header files to `/usr/local/lib/` and `/usr/local/include/` (under fbzmq sub
directory)

```
sudo make install
```

## How fbzmq works
`zmq/*` are a straight forward C++ wrappers over raw libzmq objects (like
zmq_msg_t, zmq_ctx_t, etc)  in C. ZmqEventLoop is an event loop which allows
you to attach different event callbacks on sockets as well as schedule timeouts.
Internally it maintains a dynamic poll-list which is updated only when new
`socket/fd` is added/removed and uses `zmq_poll` APIs for polling the list. It
also maintains internal heap of timeouts ordered on their expiry times.

## Full documentation
To understand how to use library, take a look at `examples/` directory. All of
our code is very well documented and refer to appropriate header files for up to
date and detailed documentation of various APIs/functionalities.

## License
fbzmq is BSD-licensed. We also provide an additional patent grant.
