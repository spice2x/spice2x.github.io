part of spiceapi;

class TouchState {
  int id;
  int x, y;

  TouchState(this.id, this.x, this.y);
}

Future<List<TouchState>> touchRead(Connection con) {
  var req = Request("touch", "read");
  return con.request(req).then((res) {

    // build states list
    List<TouchState> states = [];
    for (List state in res.getData()) {
      states.add(TouchState(
        state[0],
        state[1],
        state[2],
      ));
    }

    // return it
    return states;
  });
}

Future<void> touchWrite(Connection con, List<TouchState> states) {
  var req = Request("touch", "write");

  // add params
  for (var state in states) {
    var obj = [
      state.id,
      state.x,
      state.y
    ];
    req.addParam(obj);
  }

  return con.request(req);
}

Future<void> touchWriteReset(Connection con, List<int> touchIDs) {
  var req = Request("touch", "write_reset");

  // add params
  for (var id in touchIDs)
    req.addParam(id);

  return con.request(req);
}
