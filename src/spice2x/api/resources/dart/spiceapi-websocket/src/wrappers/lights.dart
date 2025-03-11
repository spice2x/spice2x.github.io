part of spiceapi;

class LightState {
  String name;
  double state;
  bool active;

  LightState(this.name, this.state);
  LightState._fromRead(this.name, this.state, this.active);
}

Future<List<LightState>> lightsRead(Connection con) {
  var req = Request("lights", "read");
  return con.request(req).then((res) {

    // build states list
    List<LightState> states = [];
    for (List state in res.getData()) {
      states.add(LightState._fromRead(
        state[0],
        state[1],
        state[2],
      ));
    }

    // return it
    return states;
  });
}

Future<void> lightsWrite(Connection con, List<LightState> states) {
  var req = Request("lights", "write");

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

Future<void> lightsWriteReset(Connection con, List<String> names) {
  var req = Request("lights", "write_reset");

  // add params
  for (var name in names)
    req.addParam(name);

  return con.request(req);
}
