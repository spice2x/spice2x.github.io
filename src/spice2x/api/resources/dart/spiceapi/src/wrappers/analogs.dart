part of spiceapi;

class AnalogState {
  String name;
  double state;
  bool active;

  AnalogState(this.name, this.state);
  AnalogState._fromRead(this.name, this.state, this.active);
}

Future<List<AnalogState>> analogsRead(Connection con) {
  var req = Request("analogs", "read");
  return con.request(req).then((res) {

    // build states list
    List<AnalogState> states = [];
    for (List state in res.getData()) {
      states.add(AnalogState._fromRead(
        state[0],
        state[1],
        state[2],
      ));
    }

    // return it
    return states;
  });
}

Future<void> analogsWrite(Connection con, List<AnalogState> states) {
  var req = Request("analogs", "write");

  // add params
  for (var state in states) {
    var obj = [
      state.name,
      state.state
    ];
    req.addParam(obj);
  }

  return con.request(req);
}

Future<void> analogsWriteReset(Connection con, List<String> names) {
  var req = Request("analogs", "write_reset");

  // add params
  for (var name in names)
    req.addParam(name);

  return con.request(req);
}
