part of spiceapi;

class CaptureData {
  int timestamp;
  int width, height;
  Uint8List data;
}

var _base64DecoderInstance = Base64Decoder();

Future<List> captureGetScreens(Connection con) {
  var req = Request("capture", "get_screens");
  return con.request(req).then((res) {
    return res.getData();
  });
}

Future<CaptureData> captureGetJPG(Connection con, {
  int screen = 0,
  int quality = 60,
  int divide = 1,
}) {
  var req = Request("capture", "get_jpg");
  req.addParam(screen);
  req.addParam(quality);
  req.addParam(divide);
  return con.request(req).then((res) {
    var captureData = CaptureData();
    var data = res.getData();
    if (data.length > 0) captureData.timestamp = data[0];
    if (data.length > 1) captureData.width = data[1];
    if (data.length > 2) captureData.height = data[2];
    if (data.length > 3) {
      captureData.data = _base64DecoderInstance.convert(data[3]);
    }
    return captureData;
  });
}
