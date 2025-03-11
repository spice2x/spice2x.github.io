import json
from threading import Lock


class Request:

    # global ID pool
    GLOBAL_ID = 1
    GLOBAL_ID_LOCK = Lock()

    def __init__(self, module: str, function: str, req_id=None):

        # use global ID
        with Request.GLOBAL_ID_LOCK:
            if req_id is None:

                # reset at max value
                Request.GLOBAL_ID += 1
                if Request.GLOBAL_ID >= 2 ** 64:
                    Request.GLOBAL_ID = 1

                # get ID and increase by one
                req_id = Request.GLOBAL_ID

            else:

                # carry over ID
                Request.GLOBAL_ID = req_id

        # remember ID
        self._id = req_id

        # build data dict
        self.data = {
            "id": req_id,
            "module": module,
            "function": function,
            "params": []
        }

    @staticmethod
    def from_json(request_json: str):
        req = Request("", "", 0)
        req.data = json.loads(request_json)
        req._id = req.data["id"]
        return req

    def get_id(self):
        return self._id

    def to_json(self):
        return json.dumps(
            self.data,
            ensure_ascii=False,
            check_circular=False,
            allow_nan=False,
            indent=None,
            separators=(",", ":"),
            sort_keys=False
        )

    def add_param(self, param):
        self.data["params"].append(param)
