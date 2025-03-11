from .connection import Connection
from .request import Request


def memory_write(con: Connection, dll_name: str, data: str, offset: int):
    req = Request("memory", "write")
    req.add_param(dll_name)
    req.add_param(data)
    req.add_param(offset)
    con.request(req)


def memory_read(con: Connection, dll_name: str, offset: int, size: int):
    req = Request("memory", "read")
    req.add_param(dll_name)
    req.add_param(offset)
    req.add_param(size)
    res = con.request(req)
    return res.get_data()[0]


def memory_signature(con: Connection, dll_name: str, signature: str,
                     replacement: str, offset: int, usage: int):
    req = Request("memory", "signature")
    req.add_param(dll_name)
    req.add_param(signature)
    req.add_param(replacement)
    req.add_param(offset)
    req.add_param(usage)
    res = con.request(req)
    return res.get_data()[0]
