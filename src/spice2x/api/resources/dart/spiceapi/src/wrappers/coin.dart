part of spiceapi;

Future<int> coinGet(Connection con) {
  var req = Request("coin", "get");
  return con.request(req).then((res) {
    return res.getData()[0];
  });
}

Future<void> coinSet(Connection con, int amount) {
  var req = Request("coin", "set");
  req.addParam(amount);
  return con.request(req);
}

Future<void> coinInsert(Connection con, [int amount=1]) {
  var req = Request("coin", "insert");
  if (amount != 1)
    req.addParam(amount);
  return con.request(req);
}
