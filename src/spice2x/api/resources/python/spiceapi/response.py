import json


class Response:

    def __init__(self, response_json: str):
        self._res = json.loads(response_json)
        self._id = self._res["id"]
        self._errors = self._res["errors"]
        self._data = self._res["data"]

    def to_json(self):
        return json.dumps(
            self._res,
            ensure_ascii=True,
            check_circular=False,
            allow_nan=False,
            indent=2,
            separators=(",", ": "),
            sort_keys=False
        )

    def get_id(self):
        return self._id

    def get_errors(self):
        return self._errors

    def get_data(self):
        return self._data
