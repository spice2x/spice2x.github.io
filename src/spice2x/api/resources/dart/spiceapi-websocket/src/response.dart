part of spiceapi;

class Response {

  String _json;
  int _id;
  List _errors;
  List _data;

  Response.fromJson(String json) {
    this._json = json;
    var obj = jsonDecode(json);
    this._id = obj["id"];
    this._errors = obj["errors"];
    this._data = obj["data"];
  }

  void validate() {

    // check for errors
    if (_errors.length > 0) {
      // TODO: add all errors
      throw APIError(_errors[0].toString());
    }
  }

  List getData() {
    return _data;
  }

  String toJson() {
    return _json;
  }

}
