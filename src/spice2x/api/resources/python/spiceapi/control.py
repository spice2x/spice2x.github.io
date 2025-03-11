import random
from .connection import Connection
from .request import Request


def control_raise(con: Connection, signal: str):
    req = Request("control", "raise")
    req.add_param(signal)
    con.request(req)


def control_exit(con: Connection, code=None):
    req = Request("control", "exit")
    if code:
        req.add_param(code)
    try:
        con.request(req)
    except RuntimeError:
        pass  # we expect the connection to get killed


def control_restart(con: Connection):
    req = Request("control", "restart")
    try:
        con.request(req)
    except RuntimeError:
        pass  # we expect the connection to get killed


def control_session_refresh(con: Connection):
    res = con.request(Request("control", "session_refresh", req_id=random.randint(1, 2**64)))

    # apply new password
    password = res.get_data()[0]
    con.change_password(password)


def control_shutdown(con: Connection):
    req = Request("control", "shutdown")
    try:
        con.request(req)
    except RuntimeError:
        pass  # we expect the connection to get killed


def control_reboot(con: Connection):
    req = Request("control", "reboot")
    try:
        con.request(req)
    except RuntimeError:
        pass  # we expect the connection to get killed
