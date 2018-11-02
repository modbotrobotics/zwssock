# ZWSSock - ZeroMQ over WebSockets & C

Forked from [zeromq/zwssock](https://github.com/zeromq/zwssock).


## ZWSSock

ZWSSock implements [ZWS (ZeroMQ WebSocket)](http://rfc.zeromq.org/spec:39) for use in ZeroMQ applications. Additionally it supports [Compression Extensions for WebSocket](https://tools.ietf.org/html/draft-ietf-hybi-permessage-compression-28) for per message deflate.


ZWS and ZWSSock are both in early stage and the protocol is not yet finalized nor is this library.
The API of ZWSSock is very similar to the API of zsock of CZMQ v3.0, so using it should be very simple.
While ZWSSock implements the server side of ZWS [JSMQ](https://github.com/zeromq/JSMQ) library implement ZWS on the browser side. 
[JSMQ](https://github.com/zeromq/JSMQ) is javascript library which implements the ZWS protocol and exposes ZeroMQ like API. More information is available via the [JSMQ](https://github.com/zeromq/JSMQ) repository.

With JSMQ and ZWSSock, javascript applications can talk directly to ZeroMQ applications without a webserver in the middle, however if you want to use SSL (e.g wss://someaddress) you will need a webserver or loadbalaner in the middle to terminate SSL because ZWSSock does not support SSL.

Please note that when using WebSocket it is recommended to do it over SSL because not all firewalls likes non-text protocols over port 80.

ZWSSock currently implements the router pattern. Publisher pattern is next to come (JSMQ implements subscriber and dealer).


## Usage

To include the library in your project, use the prebuilt conan package or recipe, or clone this repository and build from source.


### Examples

To use the ZWSSock library take a look at [test/c_test.c](https://github.com/modbotrobotics/zwssock/blob/master/test/c_test.c) file. 
The project also includes [browser side example](https://github.com/modbotrobotics/zwssock/blob/master/test/example.html).

## Notes

Header: `0xC2`