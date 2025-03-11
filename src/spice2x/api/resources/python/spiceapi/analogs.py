from .connection import Connection
from .request import Request


def analogs_read(con: Connection):
    res = con.request(Request("analogs", "read"))
    return res.get_data()


def analogs_write(con: Connection, analog_state_list):
    req = Request("analogs", "write")
    for state in analog_state_list:
        req.add_param(state)
    con.request(req)


def analogs_write_reset(con: Connection, analog_names=None):
    req = Request("analogs", "write_reset")

    # reset all analogs
    if not analog_names:
        con.request(req)
        return

    # reset specified analogs
    for analog_name in analog_names:
        req.add_param(analog_name)
    con.request(req)
