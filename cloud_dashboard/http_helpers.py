import socket
from flask import request
from werkzeug.exceptions import BadRequest


def query_integer(name, default, minimum):
    raw = request.args.get(name)
    if raw is None:
        return default
    try:
        value = int(raw)
    except ValueError:
        raise BadRequest(f"{name} must be an integer")
    if value < minimum:
        raise BadRequest(f"{name} must be at least {minimum}")
    return value


def json_object():
    body = request.get_json(force=True)
    if not isinstance(body, dict):
        raise BadRequest("Expected a JSON object")
    return body


def body_integer(body, name, default=0):
    value = body.get(name, default)
    if type(value) is not int or value < 0:
        raise BadRequest(f"{name} must be a nonnegative integer")
    return value


def required_name(body, name):
    value = body.get(name)
    if not isinstance(value, str) or not value.strip():
        raise BadRequest(f"{name} must be a nonempty string")
    return value.strip()


def get_lan_ip():
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
            connection.settimeout(0.1)
            connection.connect(("10.254.254.254", 1))
            return connection.getsockname()[0]
    except OSError:
        try:
            return socket.gethostbyname(socket.gethostname())
        except OSError:
            return "127.0.0.1"
