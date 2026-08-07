from .connection import Connection
from .request import Request


def sdvx_tapeled_get(con: Connection, *light_names):
    req = Request("sdvx", "tapeled_get")

    for light_name in light_names:
        req.add_param(light_name)

    res = con.request(req)
    return res.get_data()
