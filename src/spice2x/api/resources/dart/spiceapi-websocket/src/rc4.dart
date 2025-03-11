part of spiceapi;

class RC4 {

  // state
  int _a = 0;
  int _b = 0;
  List<int> _sBox = List<int>(256);

  RC4(List<int> key) {

    // init sBox
    for (int i = 0; i < 256; i++) {
      _sBox[i] = i;
    }

    // process key
    int j = 0;
    for (int i = 0; i < 256; i++) {

      // update
      j = (j + _sBox[i] + key[i % key.length]) % 256;

      // swap
      var tmp = _sBox[i];
      _sBox[i] = _sBox[j];
      _sBox[j] = tmp;
    }
  }

  void crypt(List<int> inData) {
    for (int i = 0; i < inData.length; i++) {

      // update
      _a = (_a + 1) % 256;
      _b = (_b + _sBox[_a]) % 256;

      // swap
      var tmp = _sBox[_a];
      _sBox[_a] = _sBox[_b];
      _sBox[_b] =  tmp;

      // crypt
      inData[i] ^= _sBox[(_sBox[_a] + _sBox[_b]) % 256];
    }
  }

}
