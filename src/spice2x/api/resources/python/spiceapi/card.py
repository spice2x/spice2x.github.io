from .connection import Connection
from .request import Request


def card_get_cards(con: Connection):
    return con.request(Request("card", "get_cards")).get_data()


def card_insert(con: Connection, unit: int, card_id: str):
    req = Request("card", "insert")
    req.add_param(unit)
    req.add_param(card_id)
    con.request(req)
