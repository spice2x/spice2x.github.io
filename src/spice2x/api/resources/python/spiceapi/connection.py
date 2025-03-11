import os
import socket
from .request import Request
from .response import Response
from .rc4 import rc4
from .exceptions import MalformedRequestException, APIError


class Connection:
    """ Container for managing a single connection to the API server.
    """

    def __init__(self, host: str, port: int, password: str):
        """Default constructor.

        :param host: the host string to connect to
        :param port: the port of the host
        :param password: the connection password string
        """
        self.host = host
        self.port = port
        self.password = password
        self.socket = None
        self.cipher = None
        self.reconnect()

    def reconnect(self, refresh_session=True):
        """Reconnect to the server.

        This opens a new connection and closes the previous one, if existing.
        """

        # close old socket
        self.close()

        # create new socket
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.settimeout(3)
        self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.socket.connect((self.host, self.port))

        # cipher
        self.change_password(self.password)

        # refresh session
        if refresh_session:
            from .control import control_session_refresh
            control_session_refresh(self)

    def change_password(self, password):
        """Allows to change the password on the fly.

        The cipher will be rebuilt.
        """
        if len(password) > 0:
            self.cipher = rc4(password.encode("UTF-8"))
        else:
            self.cipher = None

    def close(self):
        """Close the active connection, if existing."""

        # check if socket is existing
        if self.socket:

            # close and delete socket
            self.socket.close()
            self.socket = None

    def request(self, request: Request):
        """Send a request to the server and receive the answer.

        :param request: request object
        :return: response object
        """

        # check if disconnected
        if not self.socket:
            raise RuntimeError("No active connection.")

        # build data
        data = request.to_json().encode("UTF-8") + b"\x00"
        if self.cipher:
            data_list = list(data)
            data_cipher = []
            for b in data_list:
                data_cipher.append(b ^ next(self.cipher))
            data = bytes(data_cipher)

        # send request
        if os.name != 'nt':
            self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_QUICKACK, 1)
        self.socket.send(data)

        # get answer
        answer_data = []
        while not len(answer_data) or answer_data[-1] != 0:

            # receive data
            if os.name != 'nt':
                self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_QUICKACK, 1)
            receive_data = self.socket.recv(4096)

            # check length
            if len(receive_data):

                # check cipher
                if self.cipher:

                    # add decrypted data
                    for b in receive_data:
                        answer_data.append(int(b ^ next(self.cipher)))
                else:

                    # add plaintext
                    for b in receive_data:
                        answer_data.append(int(b))
            else:
                raise RuntimeError("Connection was closed.")

        # check for empty response
        if len(answer_data) <= 1:

            # empty response means the JSON couldn't be parsed
            raise MalformedRequestException()

        # build response
        response = Response(bytes(answer_data[:-1]).decode("UTF-8"))
        if len(response.get_errors()):
            raise APIError(response.get_errors())

        # check ID
        req_id = request.get_id()
        res_id = response.get_id()
        if req_id != res_id:
            raise RuntimeError(f"Unexpected response ID: {res_id} (expected {req_id})")

        # return response object
        return response
