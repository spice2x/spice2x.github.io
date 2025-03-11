part of spiceapi;

class ButtonState {
  String name;
  double state;
  bool active;

  ButtonState(this.name, this.state);
  ButtonState._fromRead(this.name, this.state, this.active);
}

Future<List<ButtonState>> buttonsRead(Connection con) {
  var req = Request("buttons", "read");
  return con.request(req).then((res) {

    // build states list
    List<ButtonState> states = [];
    for (List state in res.getData()) {
      states.add(ButtonState._fromRead(
        state[0],
        state[1],
        state[2],
      ));
    }

    // return it
    return states;
  });
}

Future<void> buttonsWrite(Connection con, List<ButtonState> states) {
  var req = Request("buttons", "write");

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

Future<void> buttonsWriteReset(Connection con, List<String> names) {
  var req = Request("buttons", "write_reset");

  // add params
  for (var name in names)
    req.addParam(name);

  return con.request(req);
}
