part of spiceapi;

Future<void> keypadsWrite(Connection con, int unit, String input) {
  var req = Request("keypads", "write");
  req.addParam(unit);
  req.addParam(input);
  return con.request(req);
}

Future<void> keypadsSet(Connection con, int unit, String buttons) {
  var req = Request("keypads", "set");
  req.addParam(unit);
  for (int i = 0; i < buttons.length; i++)
    req.addParam(buttons[i]);
  return con.request(req);
}

Future<String> keypadsGet(Connection con, int unit) {
  var req = Request("keypads", "get");
  req.addParam(unit);
  return con.request(req).then((res) {
    String buttons = "";
    for (var obj in res.getData()) {
      buttons += obj;
    }
    return buttons;
  });
}
