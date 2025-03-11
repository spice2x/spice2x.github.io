from .connection import Connection
from .request import Request


def keypads_write(con: Connection, keypad: int, input_values: str):
    req = Request("keypads", "write")
    req.add_param(keypad)
    req.add_param(input_values)
    con.request(req)


def keypads_set(con: Connection, keypad: int, input_values: str):
    req = Request("keypads", "set")
    req.add_param(keypad)
    for value in input_values:
        req.add_param(value)
    con.request(req)


def keypads_get(con: Connection, keypad: int):
    req = Request("keypads", "get")
    req.add_param(keypad)
    res = con.request(req)
    return res.get_data()
