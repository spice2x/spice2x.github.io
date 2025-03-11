part of spiceapi;

Future<void> controlRaise(Connection con, String signal) {
  var req = Request("control", "raise");
  req.addParam(signal);
  return con.request(req);
}

Future<void> controlExit(Connection con, int code) {
  var req = Request("control", "exit");
  req.addParam(code);
  return con.request(req);
}

Future<void> controlRestart(Connection con) {
  var req = Request("control", "restart");
  return con.request(req);
}

Future<void> controlRefreshSession(Connection con) {
  var rnd = new Random();
  var req = Request("control", "session_refresh", id: rnd.nextInt(pow(2, 32)));
  return con.request(req).then((res) {
    con.changePass(res.getData()[0]);
  });
}

Future<void> controlShutdown(Connection con) {
  var req = Request("control", "shutdown");
  return con.request(req);
}

Future<void> controlReboot(Connection con) {
  var req = Request("control", "reboot");
  return con.request(req);
}
