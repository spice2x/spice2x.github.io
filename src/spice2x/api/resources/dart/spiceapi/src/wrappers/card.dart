part of spiceapi;

class CardInfo {
  final int index;
  final String cardID;
  final String source;
  final String? fileName;

  CardInfo(this.index, this.cardID, this.source, this.fileName);
}

Future<List<CardInfo>> cardGetCards(Connection con) {
  var req = Request("card", "get_cards");
  return con.request(req).then((res) {
    List<CardInfo> cards = [];
    for (var value in res.getData()) {
      cards.add(
        CardInfo(
          value["index"],
          value["card_id"],
          value["source"],
          value["file_name"],
        ),
      );
    }
    return cards;
  });
}

Future<void> cardInsert(Connection con, int unit, String cardID) {
  var req = Request("card", "insert");
  req.addParam(unit);
  req.addParam(cardID);
  return con.request(req);
}
