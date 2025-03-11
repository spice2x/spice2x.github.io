from .connection import Connection
from .request import Request


def buttons_read(con: Connection):
    res = con.request(Request("buttons", "read"))
    return res.get_data()


def buttons_write(con: Connection, button_state_list):
    req = Request("buttons", "write")
    for state in button_state_list:
        req.add_param(state)
    con.request(req)


def buttons_write_reset(con: Connection, button_names=None):
    req = Request("buttons", "write_reset")

    # reset all buttons
    if not button_names:
        con.request(req)
        return

    # reset specified buttons
    for button_name in button_names:
        req.add_param(button_name)
    con.request(req)
