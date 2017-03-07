Zmq C++ Wrappers
=============

### Overview

This is a collection of light-weight wrappers over low-level zmq library that
aims at simplicity and type enforcement. It consists of four main classes:
Context, Message, Socket and SocketMonitor. As compared to other libraries it
has less functionality, but builds on more sophisticated abstractions from
folly library (`folly::Expected`, `Optional`, `Range`, `IOBuf`, etc).

One notable feature is lack of exception throwing - instead, all values are
returned wrapped in `folly::Expected`, which allows for applying error-code
based processing. The fbzmq::Error class wraps the error codes for zmq
operations.

### Context

This is rather straightforward, wraps zmq IO context object. Nothing too fancy,
just new naming - mainly to provide the RAII logic for zmq context.

### Message

This is the unit of information exchange in ZMQ universe. Messages are sent and
received atomically from the sockets. Multiple messages could be "chained" on
the wire using the "more" flag. A message with "more" flag set indicates that
another atomic read could be performed to retrieve the following message. The
payload of message is opaque byte array, which could be accessed directly using
data() and writeableData() methods.

Message could be constructed by direct allocation, or by wrapping an existing
IOBuf chain. In the latter case, the chain will be coalesced first, since ZMQ
can't deal with fragmented memory regions.

In addition to this, messages could be constructed from primitive types, strings
or thrift objects, using the `fromXXX()` static methods. For example:

```
// note: below will throw on error
auto msg = fbzmq::Message::fromThriftObj(obj, serializer).value();
```

Similarly, there are methods to read primitive types, strings or thrift objects
from message. Notice that all of those read messages "atomically". E.g. if you
read `uint32_t` from message, the assumption is that this is all that message
has.

### Socket

Most complicated class of them all. The socket is split in bottom half -
SocketImpl class that wraps zmq socket, and descendant template class
`Socket<type, mode>`

The `Socket<>` template allows passing the socket type and mode (server/client)
as part of the type signature. This makes reading code easier as socket
operations become more transparent. Template specializations ensure that some
socket types can only be used in fixed mode, e.g. PUB socket is always SERVER,
but DEALER could be used in CLIENT or SERVER mode.

The major difference b/w client and server modes is presence of bind/connect
methods. Servers do not have connect() and clients do no have bind methods.

Notice that Sockets only operate on Messages - you send and receive those as
atomic units. The `hasMore()` method is expected to reflect the same value as
`Message::isLast()` after the message has been received. Socket also exposes
fancy and beautiful APIs to deal with send/recv of multi-part messages.

### Polling

There is a simple poll() wrapper function added that can take arbitrary time
units for delay. The overall logic of asynchronous work with ZMQ is as
following:

- Wait for event
- Read messages as long as more flag is present on socket or
  `message.isLast() == false`
- Resume waiting

Previously we had to make read operations use timeout to avoid blocking forever
on missing messages. This logic is now being replaced by relying on "more"
flag in received messages.
