part of spiceapi;

Future<void> cardInsert(Connection con, int unit, String cardID) {
  var req = Request("card", "insert");
  req.addParam(unit);
  req.addParam(cardID);
  return con.request(req);
}
