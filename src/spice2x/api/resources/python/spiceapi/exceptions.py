

class APIError(Exception):

    def __init__(self, errors):
        super().__init__("\r\n".join(errors))


class MalformedRequestException(Exception):
    pass
