#!/usr/bin/env python3

import binascii
import spiceapi
import argparse


def patch_string(con, dll_name: str, find: str, replace: str):
    while True:
        try:

            # replace first result
            address = spiceapi.memory_signature(
                con, dll_name,
                binascii.hexlify(bytes(find, "utf-8")).decode("utf-8"),
                binascii.hexlify(bytes(replace, "utf-8")).decode("utf-8"),
                0, 0)

            # print findings
            print("{}: {} = {} => {}".format(
                dll_name,
                hex(address),
                find,
                replace))

        except spiceapi.APIError:

            # this happens when the signature wasn't found anymore
            break


def main():

    # parse args
    parser = argparse.ArgumentParser(description="SpiceAPI string replacer")
    parser.add_argument("host", type=str, help="The host to connect to")
    parser.add_argument("port", type=int, help="The port the host is using")
    parser.add_argument("password", type=str, help="The pass the host is using")
    parser.add_argument("dll", type=str, help="The DLL to patch")
    parser.add_argument("find", type=str, help="The string to find")
    parser.add_argument("replace", type=str, help="The string to replace with")
    args = parser.parse_args()

    # connect
    con = spiceapi.Connection(host=args.host, port=args.port, password=args.password)

    # replace the string
    patch_string(con, args.dll, args.find, args.replace)


if __name__ == "__main__":
    main()
