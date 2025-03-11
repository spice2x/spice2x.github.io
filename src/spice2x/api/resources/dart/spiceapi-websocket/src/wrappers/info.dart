part of spiceapi;

Future<Map> infoAVS(Connection con) {
  var req = Request("info", "avs");
  return con.request(req).then((res) {
    return res.getData()[0];
  });
}

Future<Map> infoLauncher(Connection con) {
  var req = Request("info", "launcher");
  return con.request(req).then((res) {
    return res.getData()[0];
  });
}

Future<Map> infoMemory(Connection con) {
  var req = Request("info", "memory");
  return con.request(req).then((res) {
    return res.getData()[0];
  });
}
