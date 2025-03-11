part of spiceapi;

class APIError implements Exception {
  String cause;
  APIError(this.cause);

  @override
  String toString() {
    return this.cause;
  }
}
