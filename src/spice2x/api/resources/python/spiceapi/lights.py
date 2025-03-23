from .connection import Connection
from .request import Request


def lights_read(con: Connection, light_names=None):
    req = Request("lights", "read")

    if light_names:
        for light_name in light_names:
            req.add_param(light_name)

    res = con.request(req)
    return res.get_data()


def lights_write(con: Connection, light_state_list):
    req = Request("lights", "write")
    for state in light_state_list:
        req.add_param(state)
    con.request(req)


def lights_write_reset(con: Connection, light_names=None):
    req = Request("lights", "write_reset")

    # reset all lights
    if not light_names:
        con.request(req)
        return

    # reset specified lights
    for light_name in light_names:
        req.add_param(light_name)
    con.request(req)
