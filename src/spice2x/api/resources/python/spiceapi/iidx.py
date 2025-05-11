from .connection import Connection
from .request import Request


def iidx_ticker_get(con: Connection):
    res = con.request(Request("iidx", "ticker_get"))
    return res.get_data()


def iidx_ticker_set(con: Connection, text: str):
    req = Request("iidx", "ticker_set")
    req.add_param(text)
    con.request(req)


def iidx_ticker_reset(con: Connection):
    req = Request("iidx", "ticker_reset")
    con.request(req)


def iidx_tapeled_get(con: Connection, *light_names):
    req = Request("iidx", "tapeled_get")

    for light_name in light_names:
        req.add_param(light_name)

    res = con.request(req)
    return res.get_data()
