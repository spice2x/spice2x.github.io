import base64

from .connection import Connection
from .request import Request


def capture_get_screens(con: Connection):
    res = con.request(Request("capture", "get_screens"))
    return res.get_data()


def capture_get_jpg(con: Connection, screen: int = 0, quality: int = 70, divide: int = 1):
    req = Request("capture", "get_jpg")
    req.add_param(screen)
    req.add_param(quality)
    req.add_param(divide)
    data = con.request(req).get_data()

    if len(data) < 4:
        return None

    return {
        "timestamp": data[0],
        "width": data[1],
        "height": data[2],
        "data": base64.b64decode(data[3]),
    }


def capture_get_streams(con: Connection):
    """Describes the HTTP video stream, or None when this spice2x serves none."""
    data = con.request(Request("capture", "get_streams")).get_data()
    return data[0] if data else None
