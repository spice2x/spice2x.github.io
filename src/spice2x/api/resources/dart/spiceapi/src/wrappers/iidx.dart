part of spiceapi;

Future<String> iidxTickerGet(Connection con) {
  var req = Request("iidx", "ticker_get");
  return con.request(req).then((res) {
    return res.getData()[0];
  });
}

Future<void> iidxTickerSet(Connection con, String text) {
  var req = Request("iidx", "ticker_set");
  req.addParam(text);
  return con.request(req);
}

Future<void> iidxTickerReset(Connection con) {
  var req = Request("iidx", "ticker_reset");
  return con.request(req);
}
