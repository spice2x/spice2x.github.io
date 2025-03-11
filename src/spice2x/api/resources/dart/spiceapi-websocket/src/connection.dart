part of spiceapi;


class Connection {

  // settings
  static const _TIMEOUT = Duration(seconds: 2);
  static const _BUFFER_SIZE = 1024 * 8;

  // state
  final String host, pass;
  final int port;
  var resource;
  List<int> _dataBuffer;
  StreamController<Response> _responses;
  StreamController<Connection> _connections;
  WebSocket _socket;
  RC4 _cipher;
  bool _disposed = false;

  Connection(this.host, this.port, this.pass,
      {this.resource, bool refreshSession=true}) {

    // initialize
    _dataBuffer = List<int>();
    _responses = StreamController<Response>.broadcast();
    _connections = StreamController<Connection>.broadcast();
    if (pass.length > 0)
      _cipher = RC4(utf8.encode(pass));

    // initialize socket
    this._socket = WebSocket("ws://$host:${port + 1}");
    this._socket.binaryType = "arraybuffer";

    // listen to events
    this._socket.onOpen.listen((e) async {

      // refresh session
      bool error = false;
      if (refreshSession) {
        try {
          await controlRefreshSession(this);
        } on Error {
          error = true;
        } on TimeoutException {
          error = true;
        }
      }

      // mark as connected
      if (!this._connections.isClosed)
        this._connections.add(this);
      if (error)
        this.dispose();
    });

    this._socket.onMessage.listen((e) {

      // get data
      var data = e.data;
      if (data is ByteBuffer)
        data = data.asUint8List();

      // check type
      if (data is List<int>) {

        // cipher
        if (_cipher != null)
          _cipher.crypt(data);

        // add data to buffer
        _dataBuffer.addAll(data);

        // check buffer size
        if (_dataBuffer.length > _BUFFER_SIZE) {
          this.dispose();
          return;
        }

        // check for completed message
        for (int i = 0; i < _dataBuffer.length; i++) {
          if (_dataBuffer[i] == 0) {

            // get message data and remove from buffer
            var msgData = List<int>.from(_dataBuffer.getRange(0, i));
            _dataBuffer.removeRange(0, i + 1);

            // check data length
            if (msgData.length > 0) {

              // convert to JSON
              var msgStr = utf8.decode(msgData, allowMalformed: false);

              // build response
              var res = Response.fromJson(msgStr);
              this._responses.add(res);
            }
          }
        }
      }
    });

    this._socket.onClose.listen((e) {
      this.dispose();
    });

    this._socket.onError.listen((e) {
      this.dispose();
    });
  }

  void changePass(String pass) {
    if (pass.length > 0)
      _cipher = RC4(utf8.encode(pass));
    else
      _cipher = null;
  }

  void dispose() {
    if (_socket != null)
      _socket.close();
    _socket = null;
    if (_responses != null)
      _responses.close();
    if (_connections != null)
      _connections.close();
    this._disposed = true;
    this.free();
  }

  bool isDisposed() {
    return this._disposed;
  }

  void free() {

    // release optional resource
    if (this.resource != null) {
      this.resource.release();
      this.resource = null;
    }
  }

  bool isFree() {
    return this.resource == null;
  }

  Future<Connection> onConnect() {
    return _connections.stream.first;
  }

  bool isValid() {
    return this._socket != null && !this._disposed;
  }

  Future<Response> request(Request req) {

    // add response listener
    var res = _awaitResponse(req._id);

    // write request
    _writeRequest(req);

    // return future response
    return res.then((res) {

      // validate first
      res.validate();

      // return it
      return res;
    });
  }

  void _writeRequest(Request req) async {

    // convert to JSON
    var json = req.toJson() + "\x00";
    var jsonEncoded = utf8.encode(json);

    // cipher
    if (_cipher != null)
      _cipher.crypt(jsonEncoded);

    // write to socket
    this._socket.sendByteBuffer(Int8List.fromList(jsonEncoded).buffer);
  }

  Future<Response> _awaitResponse(int id) {
    return _responses.stream.timeout(_TIMEOUT).firstWhere(
            (res) => res._id == id, orElse: null);
  }
}
