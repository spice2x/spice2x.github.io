from .connection import Connection
from .request import Request


def coin_get(con: Connection):
    res = con.request(Request("coin", "get"))
    return res.get_data()[0]


def coin_set(con: Connection, amount: int):
    req = Request("coin", "set")
    req.add_param(amount)
    con.request(req)


def coin_insert(con: Connection, amount=1):
    req = Request("coin", "insert")
    if amount != 1:
        req.add_param(amount)
    con.request(req)
