from .connection import Connection
from .request import Request


def touch_read(con: Connection):
    res = con.request(Request("touch", "read"))
    return res.get_data()


def touch_write(con: Connection, touch_points):
    req = Request("touch", "write")
    for state in touch_points:
        req.add_param(state)
    con.request(req)


def touch_write_reset(con: Connection, touch_ids):
    req = Request("touch", "write_reset")
    for touch_id in touch_ids:
        req.add_param(touch_id)
    con.request(req)
