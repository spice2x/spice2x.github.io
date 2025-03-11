part of spiceapi;

Future<void> memoryWrite(Connection con,
    String dllName, String data, int offset) {
  var req = Request("memory", "write");
  req.addParam(dllName);
  req.addParam(data);
  req.addParam(offset);
  return con.request(req);
}

Future<String> memoryRead(Connection con,
    String dllName, int offset, int size) {
  var req = Request("memory", "read");
  req.addParam(dllName);
  req.addParam(offset);
  req.addParam(size);
  return con.request(req).then((res) {
    return res.getData()[0];
  });
}

Future<int> memorySignature(Connection con,
    String dllName, String signature, String replacement,
    int offset, int usage) {
  var req = Request("memory", "signature");
  req.addParam(dllName);
  req.addParam(signature);
  req.addParam(replacement);
  req.addParam(offset);
  req.addParam(usage);
  return con.request(req).then((res) {
    return res.getData()[0];
  });
}
