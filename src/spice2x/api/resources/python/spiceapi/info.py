from .connection import Connection
from .request import Request


def info_avs(con: Connection):
    res = con.request(Request("info", "avs"))
    return res.get_data()[0]


def info_launcher(con: Connection):
    res = con.request(Request("info", "launcher"))
    return res.get_data()[0]


def info_memory(con: Connection):
    res = con.request(Request("info", "memory"))
    return res.get_data()[0]
