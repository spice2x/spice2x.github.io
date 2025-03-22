from .connection import Connection
from .request import Request

def image_resize_enable(con: Connection, enable: bool):
    req = Request("resize", "image_resize_enable")
    req.add_param(enable)
    con.request(req)

def image_resize_set_scene(con: Connection, scene: int):
    req = Request("resize", "image_resize_set_scene")
    req.add_param(scene)
    con.request(req)
