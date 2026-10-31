"""Flask application factory and executable entry point.

Importing this module performs no IO and starts no background threads.
"""
import logging
import os
from pathlib import Path
import sys
import signal

from flask import Flask
from admin_routes import create_admin_api
from routes import create_api
from runtime import DashboardRuntime


def create_app(config=None, *, runtime=None, initialize=True):
    app = Flask(__name__)
    if config:
        app.config.update(config)
    runtime = runtime if runtime is not None else DashboardRuntime()
    if initialize:
        runtime.initialize()
    app.extensions["dashboard"] = runtime
    app.register_blueprint(create_api(runtime))
    app.register_blueprint(create_admin_api(runtime))
    return app


def configure_logging():
    logger = logging.getLogger("dashboard")
    if any(getattr(handler, "edgevision_handler", False) for handler in logger.handlers):
        return logger
    logger.setLevel(logging.INFO)
    handlers = [logging.FileHandler(Path(__file__).with_name("dashboard.log"), encoding="utf-8"),
                logging.StreamHandler(sys.stdout)]
    for handler in handlers:
        handler.edgevision_handler = True
        handler.setFormatter(logging.Formatter("[%(asctime)s] %(message)s", datefmt="%H:%M:%S"))
        logger.addHandler(handler)
    return logger


def main():
    logger = configure_logging()
    application = create_app()
    runtime = application.extensions["dashboard"]
    def terminate(signum, frame):
        raise SystemExit(0)

    previous_sigterm = signal.signal(signal.SIGTERM, terminate)
    try:
        runtime.start()
        port = int(os.environ.get("PORT", 5000))
        try:
            from waitress import serve
        except ImportError:
            logger.info("Starting Flask on port %s", port)
            application.run(host="0.0.0.0", port=port, debug=False, threaded=True)
        else:
            logger.info("Starting waitress on port %s", port)
            serve(application, host="0.0.0.0", port=port, threads=8)
    finally:
        try:
            runtime.stop()
        finally:
            signal.signal(signal.SIGTERM, previous_sigterm)


if __name__ == "__main__":
    main()
